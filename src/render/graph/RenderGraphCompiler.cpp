#include "render/graph/RenderGraphCompiler.h"

#include "core/DirectedGraphTraversal.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/graph/RenderGraph.h"

#include <algorithm>
#include <unordered_set>

namespace
{
using Visibility = RenderPassReflection::Visibility;

bool sameConnections(const RenderPassReflection& a, const RenderPassReflection& b)
{
    if (a.fields().size() != b.fields().size())
        return false;

    return std::ranges::all_of(
        a.fields(),
        [&](const auto& field)
        {
            const auto* other = b.getField(field.name);
            return other && other->visibility == field.visibility &&
                ResourceCache::sameDescription(field.resource, other->resource);
        }
    );
}
}  // namespace

std::unique_ptr<RenderGraphExe>
RenderGraphCompiler::compile(const RenderGraph& graph, RenderContext* context, const Dependencies& dependencies)
{
    RenderGraphCompiler compiler(graph, dependencies);
    auto                executable = std::make_unique<RenderGraphExe>();
    std::string         log;
    {
        const logger::ScopedErrorLog errors(log);
        compiler.preparePasses();
        if (log.empty())
            compiler.resolveResources(executable->resources);
        if (log.empty())
        {
            for (const auto& data : compiler.passes)
            {
                std::string passLog;
                {
                    const logger::ScopedErrorLog passErrors(passLog);
                    data.pass->compile(context, data.compileData);
                }
                if (!passLog.empty())
                    LOG_ERROR("{}: {}", data.name, passLog);
            }
        }
    }
    if (!log.empty())
    {
        LOG_ERROR("{}", log);
        return {};
    }

    executable->resources.allocateResources(*graph.mpDevice, dependencies.defaultResourceProps);
    executable->outputs.reserve(graph.mOutputs.size());
    for (size_t index = 0; index < graph.mOutputs.size(); ++index)
        executable->outputs.push_back(*executable->resources.findSlot(graph.getOutputName(index)));
    executable->passes.reserve(compiler.passes.size());
    for (const auto& data : compiler.passes)
    {
        ResourceCache::Ports ports;
        ports.reserve(data.reflection.fields().size());
        for (const auto& field : data.reflection.fields())
            if (const auto slot = executable->resources.findSlot(data.name + '.' + field.name))
                ports.emplace_back(field.name, *slot);
        executable->passes.push_back({data.pass, std::move(ports)});
        executable->maxFramesInFlight = std::min(executable->maxFramesInFlight, data.pass->getMaxFramesInFlight());
    }
    return executable;
}

void RenderGraphCompiler::preparePasses()
{
    if (graph.mOutputs.empty())
    {
        LOG_ERROR("render graph '{}' has no marked outputs", graph.getName());
        return;
    }

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
        RenderPass::CompileData              compileData{defaults.dims, defaults.format};
        auto                                 reflection = data.pPass->reflect(compileData);
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
            {index, data.name, data.pPass, std::move(reflection), std::move(compileData), std::move(usedOutputs)}
        );
    }
}

void RenderGraphCompiler::resolveResources(ResourceCache& resources)
{
    ResourceCache::DefaultProperties defaults;
    bool                             useDefaults = false;
    // Let concrete requirements propagate before substituting graph defaults.
    for (size_t round = 0; round <= 4 * passes.size() + 1; ++round)
    {
        ResourceCache connections;
        registerResources(connections, false);

        bool changed = false;
        for (auto& data : passes)
        {
            RenderPassReflection connected;
            for (const auto& field : data.reflection.fields())
            {
                const bool input = is_set(field.visibility, Visibility::Input);
                const bool output =
                    data.usedOutputs.contains(field.name) && is_set(field.visibility, Visibility::Output);
                if (!input && !output)
                    continue;

                const auto slot = connections.findSlot(data.name + '.' + field.name);
                if (!slot)
                    continue;
                auto desc = connections.description(*slot, defaults);
                if (desc.index() != field.resource.index())
                    continue;

                if (input && output)
                    connected.addInputOutput(field.name, std::move(desc));
                else if (input)
                    connected.addInput(field.name, std::move(desc));
                else
                    connected.addOutput(field.name, std::move(desc));
            }

            if (sameConnections(data.compileData.connectedResources, connected))
                continue;

            changed                             = true;
            data.compileData.connectedResources = std::move(connected);
            data.reflection                     = data.pass->reflect(data.compileData);
        }
        if (logger::ScopedErrorLog::hasErrors())
            return;
        if (changed)
            continue;
        if (!useDefaults)
        {
            defaults    = dependencies.defaultResourceProps;
            useDefaults = true;
            continue;
        }

        validateGraph();
        registerResources(resources, true);
        if (!logger::ScopedErrorLog::hasErrors())
            for (size_t index = 0; index < graph.mOutputs.size(); ++index)
                resources.markOutput(graph.getOutputName(index));
        return;
    }
    LOG_ERROR("resource descriptions did not converge for render graph '{}'", graph.getName());
}

void RenderGraphCompiler::validateGraph() const
{
    for (const auto& data : passes)
    {
        for (const auto& field : data.reflection.fields())
            if (is_set(field.visibility, Visibility::Input) && !field.optional &&
                !data.compileData.connectedResources.getField(field.name))
                LOG_ERROR("missing graph input '{}.{}'", data.name, field.name);
    }

    for (const auto& output : graph.mOutputs)
    {
        const auto& data  = *std::ranges::find(passes, output.nodeId, &PassData::index);
        const auto* field = data.reflection.getField(output.field);
        if (!field || !is_set(field->visibility, Visibility::Output))
            LOG_ERROR("unknown output '{}.{}'", data.name, output.field);
    }

    for (const auto& [name, resource] : dependencies.externalResources)
    {
        const auto dot  = name.rfind('.');
        const auto id   = graph.getPassIndex(name.substr(0, dot));
        const auto pass = std::ranges::find(passes, id, &PassData::index);
        if (pass == passes.end())
            continue;
        const auto* field = pass->reflection.getField(std::string_view(name).substr(dot + 1));
        if (!field || !is_set(field->visibility, Visibility::Input))
            LOG_ERROR("unknown input '{}'", name);
    }
}

void RenderGraphCompiler::registerResources(ResourceCache& resources, bool validate) const
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
            if (validate)
            {
                if (!output || !is_set(output->visibility, Visibility::Output))
                    LOG_ERROR("unknown output '{}'", source);
                if (!input || !is_set(input->visibility, Visibility::Input))
                    LOG_ERROR("unknown input '{}'", target);
                if (dependencies.externalResources.contains(target))
                    LOG_ERROR("graph input '{}' has an edge and an external resource", target);
            }
            if (input && is_set(input->visibility, Visibility::Input) && output &&
                is_set(output->visibility, Visibility::Output) && resources.findSlot(source))
                resources.connect(target, source, input->resource, validate);
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
                    resources.addInput(name, field.resource, external->second.get(), validate);
            }
            else if (!field.optional || data.usedOutputs.contains(field.name))
                resources.addResource(name, field.resource);
        }
    }
}
