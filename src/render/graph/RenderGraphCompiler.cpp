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
using Field = RenderPassReflection::Field;
using ResourceDesc = RenderPassReflection::ResourceDesc;

ResourceDesc inputResourceDesc(const Field& field, rhi::IResource& resource)
{
    if (std::holds_alternative<rhi::BufferDesc>(field.resource))
    {
        rhi::ComPtr<rhi::IBuffer> buffer;
        if (SLANG_SUCCEEDED(resource.queryInterface(rhi::IBuffer::getTypeGuid(),
                reinterpret_cast<void**>(buffer.writeRef()))))
            return buffer->getDesc();
    }
    else
    {
        rhi::ComPtr<rhi::ITexture> texture;
        if (SLANG_SUCCEEDED(resource.queryInterface(rhi::ITexture::getTypeGuid(),
                reinterpret_cast<void**>(texture.writeRef()))))
        {
            const auto& desc = texture->getDesc();
            const auto format = field.texture().format;
            if (format != rhi::Format::Undefined && format != desc.format)
                LOG_ERROR("incompatible external texture format for '{}'", field.name);
            return desc;
        }
    }

    LOG_ERROR("incompatible external resource for '{}'", field.name);
    return field.resource;
}
}

std::unique_ptr<RenderGraphExe>
RenderGraphCompiler::compile(const RenderGraph& graph, RenderContext* context, const Dependencies& dependencies)
{
    RenderGraphCompiler compiler(graph, dependencies);
    auto executable = std::make_unique<RenderGraphExe>();
    std::string log;
    {
        const logger::ScopedErrorLog errors(log);
        compiler.preparePasses();
        if (log.empty())
            compiler.validateGraph();
        if (log.empty())
            compiler.registerResources(executable->resources);
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

    executable->resources.allocateResources(*context->device, dependencies.defaultResourceProps);
    executable->defaultTexDims = dependencies.defaultResourceProps.dims;
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

        const auto& data = graph.mNodeData.at(index);
        const auto& node = *graph.mpGraph->getNode(index);
        RenderPass::CompileData compileData{defaults.dims, defaults.format};
        const auto declarations = data.pPass->reflect(compileData);

        for (uint32_t incoming = 0; incoming < node.getIncomingEdgeCount(); ++incoming)
        {
            const auto edgeId = node.getIncomingEdge(incoming);
            const auto& connection = graph.mEdgeData.at(edgeId);
            if (connection.dstField.empty())
                continue;

            const auto producerId = graph.mpGraph->getEdge(edgeId)->getSourceNode();
            const auto& producer = *std::ranges::find(passes, producerId, &PassData::index);
            const auto* output = producer.reflection.getField(connection.srcField);
            const auto* input = declarations.getField(connection.dstField);
            if (!output || !is_set(output->visibility, Visibility::Output))
            {
                LOG_ERROR("unknown output '{}.{}'", producer.name, connection.srcField);
                continue;
            }
            if (!input || !is_set(input->visibility, Visibility::Input) || input->resource.index() != output->resource.index())
            {
                LOG_ERROR("incompatible input '{}.{}'", data.name, connection.dstField);
                continue;
            }

            auto desc = output->resource;
            if (auto* texture = std::get_if<rhi::TextureDesc>(&desc); texture && texture->format == rhi::Format::Undefined)
                texture->format = defaults.format;
            compileData.connectedResources.addInput(input->name, std::move(desc));
        }

        for (const auto& field : declarations.fields())
        {
            if (!is_set(field.visibility, Visibility::Input))
                continue;
            const auto name = data.name + '.' + field.name;
            const auto external = dependencies.externalResources.find(name);
            if (external == dependencies.externalResources.end())
                continue;
            if (compileData.connectedResources.getField(field.name))
                LOG_ERROR("graph input '{}' has an edge and an external resource", name);
            else
                compileData.connectedResources.addInput(field.name, inputResourceDesc(field, *external->second));
        }

        if (logger::ScopedErrorLog::hasErrors())
            return;
        auto reflection = data.pPass->reflect(compileData);
        passes.push_back({index, data.name, data.pPass, std::move(reflection), std::move(compileData)});
    }
}

void RenderGraphCompiler::validateGraph() const
{
    if (graph.mOutputs.empty())
        LOG_ERROR("render graph '{}' has no marked outputs", graph.getName());

    for (const auto& data : passes)
    {
        for (const auto& field : data.reflection.fields())
            if (is_set(field.visibility, Visibility::Input) && !field.optional &&
                !data.compileData.connectedResources.getField(field.name))
                LOG_ERROR("missing graph input '{}.{}'", data.name, field.name);
        for (const auto& connected : data.compileData.connectedResources.fields())
        {
            const auto* field = data.reflection.getField(connected.name);
            if (!field || !is_set(field->visibility, Visibility::Input) ||
                field->resource.index() != connected.resource.index())
                LOG_ERROR("incompatible input '{}.{}'", data.name, connected.name);
        }
    }

    for (const auto& output : graph.mOutputs)
    {
        const auto& data = *std::ranges::find(passes, output.nodeId, &PassData::index);
        const auto* field = data.reflection.getField(output.field);
        if (!field || !is_set(field->visibility, Visibility::Output))
            LOG_ERROR("unknown output '{}.{}'", data.name, output.field);
    }
}

void RenderGraphCompiler::registerResources(ResourceCache& resources) const
{
    for (const auto& data : passes)
    {
        const auto& node = *graph.mpGraph->getNode(data.index);
        std::unordered_set<std::string_view> connected;
        for (uint32_t incoming = 0; incoming < node.getIncomingEdgeCount(); ++incoming)
        {
            const auto edgeId = node.getIncomingEdge(incoming);
            const auto& connection = graph.mEdgeData.at(edgeId);
            if (connection.dstField.empty())
                continue;

            const auto producerId = graph.mpGraph->getEdge(edgeId)->getSourceNode();
            const auto& producer = graph.mNodeData.at(producerId);
            const auto& input = *data.reflection.getField(connection.dstField);
            resources.connect(data.name + '.' + input.name, producer.name + '.' + connection.srcField, input.resource);
            connected.insert(input.name);
        }

        for (const auto& field : data.reflection.fields())
        {
            if (connected.contains(field.name))
                continue;
            const auto name = data.name + '.' + field.name;
            if (is_set(field.visibility, Visibility::Input))
            {
                const auto external = dependencies.externalResources.find(name);
                auto* resource = external != dependencies.externalResources.end() ? external->second.get() : nullptr;
                resources.addResource(name, field.resource, resource, false);
            }
            else
            {
                if (field.optional && !graph.isGraphOutput({data.index, field.name}))
                {
                    bool used = false;
                    for (uint32_t outgoing = 0; outgoing < node.getOutgoingEdgeCount(); ++outgoing)
                        used |= graph.mEdgeData.at(node.getOutgoingEdge(outgoing)).srcField == field.name;
                    if (!used)
                        continue;
                }
                resources.addResource(name, field.resource, nullptr, true);
            }
        }
    }

    for (size_t index = 0; index < graph.mOutputs.size(); ++index)
        resources.markOutput(graph.getOutputName(index));
}
