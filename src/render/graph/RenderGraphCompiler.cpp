#include "render/graph/RenderGraphCompiler.h"

#include "core/ds/DirectedGraphTraversal.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/graph/RenderGraph.h"

#include <algorithm>
#include <unordered_set>

namespace
{
using Visibility = RenderPassReflection::Visibility;
}  // namespace

std::unique_ptr<RenderGraphExe>
RenderGraphCompiler::compile(const RenderGraph& graph, RenderContext* context, const Dependencies& dependencies)
{
    RenderGraphCompiler compiler(graph, dependencies);
    auto                executable = std::make_unique<RenderGraphExe>();

    compiler.preparePasses();
    compiler.resolveResources(executable->resources);

    executable->passes.reserve(compiler.passes.size());
    for (const auto& data : compiler.passes)
    {
        ResourceCache::Ports ports;
        ports.reserve(data.reflection.fields().size());
        for (const auto& field : data.reflection.fields())
            if (const auto slot = executable->resources.findSlot(data.name + '.' + field.name))
                ports.emplace_back(field.name, *slot);

        data.pass->compile(context, {data.name, executable->resources, ports});
        executable->passes.push_back({data.pass, std::move(ports)});
        executable->maxFramesInFlight = std::min(executable->maxFramesInFlight, data.pass->getMaxFramesInFlight());
    }

    executable->resources.allocateResources(*graph.mpDevice);
    executable->outputs.reserve(graph.mOutputs.size());
    for (size_t index = 0; index < graph.mOutputs.size(); ++index)
        executable->outputs.push_back(*executable->resources.findSlot(graph.getOutputName(index)));

    return executable;
}

void RenderGraphCompiler::preparePasses()
{
    CHECK(!graph.mOutputs.empty(), "render graph '{}' has no marked outputs", graph.getName());

    std::unordered_set<uint32_t> roots;
    for (const auto& output : graph.mOutputs)
        roots.insert(output.nodeId);
    for (const auto& [index, data] : graph.mEdgeData)
    {
        if (data.srcField.empty())
        {
            const auto& edge = *graph.mpGraph->getEdge(index);
            roots.insert(edge.getSourceNode());
            roots.insert(edge.getDestNode());
        }
    }

    using Flags = DirectedGraphTraversal::Flags;
    std::unordered_set<uint32_t> participating;
    for (const auto root : roots)
    {
        if (participating.contains(root))
            continue;
        DirectedGraphDfsTraversal traversal(*graph.mpGraph, root, Flags::Reverse | Flags::IgnoreVisited);
        for (auto index = traversal.traverse(); index != DirectedGraph::kInvalidID; index = traversal.traverse())
            participating.insert(index);
    }

    const auto& defaults = dependencies.defaultResourceProps;
    passes.reserve(participating.size());
    for (const auto index : DirectedGraphTopologicalSort::sort(*graph.mpGraph))
    {
        if (!participating.contains(index))
            continue;

        const auto&                          data = graph.mNodeData.at(index);
        auto                                 reflection = data.pPass->reflect(defaults);
        reflection.applyGraphTextureProperties({defaults.dims.x, defaults.dims.y, 1}, defaults.format);
        std::unordered_set<std::string_view> usedOutputs;
        const auto&                          node = *graph.mpGraph->getNode(index);
        for (uint32_t outgoing = 0; outgoing < node.getOutgoingEdgeCount(); ++outgoing)
        {
            const auto  edgeId     = node.getOutgoingEdge(outgoing);
            const auto& connection = graph.mEdgeData.at(edgeId);
            if (!connection.srcField.empty() && participating.contains(graph.mpGraph->getEdge(edgeId)->getDestNode()))
                usedOutputs.insert(connection.srcField);
        }
        for (const auto& output : graph.mOutputs)
            if (output.nodeId == index)
                usedOutputs.insert(output.field);

        passes.push_back(
            {index, data.name, data.pPass, std::move(reflection), std::move(usedOutputs)}
        );
    }
}

void RenderGraphCompiler::resolveResources(ResourceCache& resources)
{
    registerResources(resources);
    validateGraph(resources);

    const auto& defaults = dependencies.defaultResourceProps;
    // Connections have applied all declared requirements. Match inputs in execution order.
    for (const auto& data : passes)
    {
        for (const auto& field : data.reflection.fields())
        {
            if (field.matchInput.empty())
                continue;

            if (const auto target = resources.findSlot(data.name + '.' + field.name))
            {
                const auto source = *resources.findSlot(data.name + '.' + field.matchInput);
                resources.matchTexture(*target, source, field.textureMatch, defaults);
            }
        }
    }

    resources.applyTextureDefaults(defaults);
    for (size_t index = 0; index < graph.mOutputs.size(); ++index)
        resources.markOutput(graph.getOutputName(index));
}

void RenderGraphCompiler::validateGraph(const ResourceCache& resources) const
{
    for (const auto& data : passes)
    {
        for (const auto& field : data.reflection.fields())
            if (is_set(field.visibility, Visibility::Input) && !field.optional)
                CHECK(resources.findSlot(data.name + '.' + field.name), "missing graph input '{}.{}'", data.name, field.name);
    }

    for (const auto& output : graph.mOutputs)
    {
        const auto& data  = *std::ranges::find(passes, output.nodeId, &PassData::index);
        const auto* field = data.reflection.getField(output.field);
        CHECK(field && is_set(field->visibility, Visibility::Output), "unknown output '{}.{}'", data.name, output.field);
    }

    for (const auto& [name, resource] : dependencies.externalResources)
    {
        const auto dot  = name.rfind('.');
        const auto id   = graph.getPassIndex(name.substr(0, dot));
        const auto pass = std::ranges::find(passes, id, &PassData::index);
        if (pass == passes.end())
            continue;
        const auto* field = pass->reflection.getField(std::string_view(name).substr(dot + 1));
        CHECK(field && is_set(field->visibility, Visibility::Input), "unknown input '{}'", name);
    }
}

void RenderGraphCompiler::registerResources(ResourceCache& resources) const
{
    for (const auto& data : passes)
    {
        const auto&                          node = *graph.mpGraph->getNode(data.index);
        std::unordered_set<std::string_view> connected;
        for (uint32_t incoming = 0; incoming < node.getIncomingEdgeCount(); ++incoming)
        {
            const auto  edgeId     = node.getIncomingEdge(incoming);
            const auto& connection = graph.mEdgeData.at(edgeId);
            if (connection.dstField.empty())
                continue;

            const auto* input      = data.reflection.getField(connection.dstField);
            const auto  producerId = graph.mpGraph->getEdge(edgeId)->getSourceNode();
            const auto& producer   = *std::ranges::find(passes, producerId, &PassData::index);
            const auto* output     = producer.reflection.getField(connection.srcField);
            const auto  source     = producer.name + '.' + connection.srcField;
            const auto  target     = data.name + '.' + connection.dstField;
            CHECK(output && is_set(output->visibility, Visibility::Output), "unknown output '{}'", source);
            CHECK(input && is_set(input->visibility, Visibility::Input), "unknown input '{}'", target);
            CHECK(
                !dependencies.externalResources.contains(target),
                "graph input '{}' has an edge and an external resource",
                target
            );

            if (resources.findSlot(source))
                resources.connect(target, source, input->resource);
            connected.insert(connection.dstField);
        }

        for (const auto& field : data.reflection.fields())
        {
            if (connected.contains(field.name))
                continue;
            const auto name = data.name + '.' + field.name;
            if (is_set(field.visibility, Visibility::Input))
            {
                const auto external = dependencies.externalResources.find(name);
                if (external != dependencies.externalResources.end())
                    resources.addInput(name, field.resource, external->second.get());
            }
            else if (!field.optional || data.usedOutputs.contains(field.name))
                resources.addResource(name, field.resource);
        }
    }
}
