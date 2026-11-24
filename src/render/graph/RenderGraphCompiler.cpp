#include "render/graph/RenderGraphCompiler.h"

#include <slang-rhi.h>
#include "core/DirectedGraphTraversal.h"
#include "core/Logger.h"
#include "render/RenderContext.h"
#include "render/graph/RenderGraph.h"
#include "render/passes/ResolvePass.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace
{
    using Field      = RenderPassReflection::Field;
    using Visibility = Field::Visibility;

    bool canAutoResolve(const Field& source, const Field& target)
    {
        return source.getSampleCount() > 1 && target.getSampleCount() == 1;
    }
}  // namespace

RenderGraphCompiler::RenderGraphCompiler(RenderGraph& graph, const Dependencies& dependencies) :
    mGraph(graph), mpDevice(graph.getDevice()), mDependencies(dependencies)
{
}

std::unique_ptr<RenderGraphExe>
RenderGraphCompiler::compile(RenderGraph& graph, RenderContext* context, const Dependencies& dependencies)
{
    RenderGraphCompiler compiler(graph, dependencies);
    auto                resourceCache = std::make_unique<ResourceCache>();
    for (const auto& [name, resource] : dependencies.externalResources)
        resourceCache->registerExternalResource(name, resource);

    std::string log;
    {
        const logger::ScopedErrorLog errors(log);
        compiler.resolveExecutionOrder();
        if (log.empty())
            compiler.validateGraph();
        if (log.empty())
            compiler.validateDeviceFeatures();
        if (log.empty())
            compiler.compilePasses(context);
        if (log.empty() && compiler.insertAutoPasses())
        {
            compiler.resolveExecutionOrder();
            if (log.empty())
                compiler.validateGraph();
        }
        if (log.empty())
            compiler.allocateResources(context->device, resourceCache.get());
    }

    compiler.restoreCompilationChanges();
    if (!log.empty())
    {
        LOG_ERROR("{}", log);
        return {};
    }

    auto executable = std::make_unique<RenderGraphExe>();
    executable->mpResourceCache = std::move(resourceCache);
    executable->mSceneInputs    = compiler.mSceneInputs;
    executable->mExecutionList.reserve(compiler.mExecutionList.size());
    for (const auto& pass : compiler.mExecutionList)
    {
        executable->insertPass(pass.name, pass.pPass);
        executable->mMaxFramesInFlight = std::min(executable->mMaxFramesInFlight, pass.pPass->getMaxFramesInFlight());
    }
    return executable;
}

void RenderGraphCompiler::resolveExecutionOrder()
{
    std::unordered_set<uint32_t> mandatoryPasses;
    for (const auto& output : mGraph.mOutputs)
        mandatoryPasses.insert(output.nodeId);
    for (const auto& [index, data] : mGraph.mEdgeData)
    {
        if (data.srcField.empty())
        {
            const auto& edge = *mGraph.mpGraph->getEdge(index);
            mandatoryPasses.insert(edge.getSourceNode());
            mandatoryPasses.insert(edge.getDestNode());
        }
    }

    using Flags = DirectedGraphTraversal::Flags;
    std::unordered_set<uint32_t> participatingPasses;
    for (const auto root : mandatoryPasses)
    {
        if (participatingPasses.contains(root))
            continue;
        DirectedGraphDfsTraversal traversal(*mGraph.mpGraph, root, Flags::Reverse | Flags::IgnoreVisited);
        for (auto index = traversal.traverse(); index != DirectedGraph::kInvalidID; index = traversal.traverse())
            participatingPasses.insert(index);
    }

    const auto&                   properties = mDependencies.defaultResourceProps;
    const RenderPass::CompileData defaults{properties.dims, properties.format};
    mExecutionList.clear();
    mExecutionList.reserve(participatingPasses.size());
    mSceneInputs = SceneInputs::None;
    for (const auto index : DirectedGraphTopologicalSort::sort(*mGraph.mpGraph))
    {
        if (participatingPasses.contains(index))
        {
            const auto& node = mGraph.mNodeData.at(index);
            mSceneInputs |= node.pPass->getSceneInputs();
            mExecutionList.push_back({index, node.pPass, node.name, node.pPass->reflect(defaults)});
        }
    }

    for (auto& pass : mExecutionList)
    {
        const auto data = prepPassCompilationData(pass);
        if (logger::ScopedErrorLog::hasErrors())
            return;
        pass.reflector = pass.pPass->reflect(data);
    }
}

bool RenderGraphCompiler::insertAutoPasses()
{
    for (const auto& pass : mExecutionList)
    {
        for (size_t index = 0; index < pass.reflector.getFieldCount(); ++index)
        {
            const auto& source = *pass.reflector.getField(index);
            if (!is_set(source.getVisibility(), Visibility::Output) || source.getSampleCount() <= 1)
                continue;

            const auto&              node = *mGraph.mpGraph->getNode(pass.index);
            std::vector<std::string> destinations;
            for (uint32_t edgeIndex = 0; edgeIndex < node.getOutgoingEdgeCount(); ++edgeIndex)
            {
                const auto  edgeId = node.getOutgoingEdge(edgeIndex);
                const auto& data   = mGraph.mEdgeData.at(edgeId);
                if (data.srcField != source.getName())
                    continue;

                const auto targetId = mGraph.mpGraph->getEdge(edgeId)->getDestNode();
                const auto target   = std::ranges::find(mExecutionList, targetId, &PassData::index);
                if (target == mExecutionList.end())
                    continue;
                const auto* input = target->reflector.getField(data.dstField);
                if (input && is_set(input->getVisibility(), Visibility::Input) && canAutoResolve(source, *input))
                    destinations.push_back(target->name + '.' + data.dstField);
            }

            if (destinations.empty())
                continue;

            const auto sourceName  = pass.name + '.' + source.getName();
            const auto resolveName = sourceName + "-ResolvePass";
            const auto resolve     = ResolvePass::create(mpDevice);
            resolve->setFormat(source.getFormat());
            mGraph.addPass(resolve, resolveName);
            mGraph.addEdge(sourceName, resolveName + ".src");
            for (const auto& destination : destinations)
            {
                mGraph.removeEdge(sourceName, destination);
                mGraph.addEdge(resolveName + ".dst", destination);
                mCompilationChanges.removedEdges.emplace_back(sourceName, destination);
            }
            mCompilationChanges.generatedPasses.push_back(resolveName);
        }
    }
    return !mCompilationChanges.generatedPasses.empty();
}

void RenderGraphCompiler::validateGraph() const
{
    if (mGraph.mOutputs.empty())
        LOG_ERROR("render graph '{}' has no marked outputs", mGraph.getName());

    for (const auto& pass : mExecutionList)
    {
        const auto&                          node = *mGraph.mpGraph->getNode(pass.index);
        std::unordered_set<std::string_view> inputs;
        for (uint32_t index = 0; index < node.getIncomingEdgeCount(); ++index)
        {
            const auto  edgeId = node.getIncomingEdge(index);
            const auto& data   = mGraph.mEdgeData.at(edgeId);
            if (data.dstField.empty())
                continue;

            inputs.insert(data.dstField);
        }

        for (size_t index = 0; index < pass.reflector.getFieldCount(); ++index)
        {
            const auto& field = *pass.reflector.getField(index);
            if (!is_set(field.getVisibility(), Visibility::Input))
                continue;

            const auto name      = pass.name + '.' + field.getName();
            const bool connected = inputs.erase(field.getName()) != 0;
            const bool external  = mDependencies.externalResources.contains(name);
            if (connected && external)
                LOG_ERROR("graph input '{}' has an edge and an external resource", name);
            if (!connected && !external && !is_set(field.getFlags(), Field::Flags::Optional))
                LOG_ERROR("missing graph input '{}'", name);
        }

        for (const auto input : inputs)
            LOG_ERROR("unknown port '{}.{}'", pass.name, input);
    }

    for (const auto& output : mGraph.mOutputs)
    {
        const auto pass = std::ranges::find(mExecutionList, output.nodeId, &PassData::index);

        const auto* field = pass->reflector.getField(output.field);
        if (!field || !is_set(field->getVisibility(), Visibility::Output))
            LOG_ERROR("unknown port '{}.{}'", pass->name, output.field);
    }
}

void RenderGraphCompiler::validateDeviceFeatures() const
{
    if (is_set(mSceneInputs, SceneInputs::RayTracing) && !mpDevice->hasFeature(rhi::Feature::RayTracing))
        LOG_ERROR("render graph '{}' requires ray tracing", mGraph.getName());
}

void RenderGraphCompiler::compilePasses(RenderContext* context)
{
    std::string log;
    std::string passLog;
    for (;;)
    {
        log.clear();
        {
            const logger::ScopedErrorLog errors(passLog);
            for (const auto& pass : mExecutionList)
            {
                const auto data = prepPassCompilationData(pass);
                if (passLog.empty())
                    pass.pPass->compile(context, data);
                if (!passLog.empty())
                {
                    std::format_to(std::back_inserter(log), "{}: {}", pass.name, passLog);
                    passLog.clear();
                }
            }
        }
        if (log.empty())
            return;

        bool changed = false;
        for (auto& pass : mExecutionList)
        {
            const auto data = prepPassCompilationData(pass);
            if (logger::ScopedErrorLog::hasErrors())
                break;
            auto reflector = pass.pPass->reflect(data);
            if (logger::ScopedErrorLog::hasErrors())
                break;
            if (reflector != pass.reflector)
            {
                pass.reflector = std::move(reflector);
                changed        = true;
            }
        }
        if (logger::ScopedErrorLog::hasErrors() || !changed)
        {
            LOG_ERROR("graph compilation failed:\n{}", log);
            return;
        }

        validateGraph();
        if (logger::ScopedErrorLog::hasErrors())
            return;
    }
}

RenderPass::CompileData RenderGraphCompiler::prepPassCompilationData(const PassData& pass)
{
    const auto getField = [](const PassData& owner, const std::string& name, Visibility visibility) -> const Field*
    {
        const auto* field = owner.reflector.getField(name);
        if (field && is_set(field->getVisibility(), visibility))
            return field;

        LOG_ERROR("unknown port '{}.{}'", owner.name, name);
        return nullptr;
    };

    const auto&             properties = mDependencies.defaultResourceProps;
    RenderPass::CompileData data{properties.dims, properties.format};
    const auto&             node = *mGraph.mpGraph->getNode(pass.index);
    for (uint32_t index = 0; index < node.getIncomingEdgeCount(); ++index)
    {
        const auto  edgeId     = node.getIncomingEdge(index);
        const auto& connection = mGraph.mEdgeData.at(edgeId);
        if (connection.dstField.empty())
            continue;

        const auto sourceId = mGraph.mpGraph->getEdge(edgeId)->getSourceNode();
        const auto source   = std::ranges::find(mExecutionList, sourceId, &PassData::index);

        const auto* output = getField(*source, connection.srcField, Visibility::Output);
        const auto* input  = getField(pass, connection.dstField, Visibility::Input);
        if (!output || !input)
            return data;

        auto field = *output;
        if (field.getType() != Field::Type::RawBuffer && field.getFormat() == rhi::Format::Undefined)
            field.format(properties.format);
        data.connectedResources.addField(
            field.name(input->getName()).desc(input->getDesc()).visibility(input->getVisibility())
        );
    }

    const auto prefix = pass.name + '.';
    for (const auto& [name, resource] : mDependencies.externalResources)
    {
        if (name.rfind('.') != pass.name.size() || !name.starts_with(prefix))
            continue;
        const auto  fieldName = name.substr(prefix.size());
        const auto* input     = getField(pass, fieldName, Visibility::Input);
        if (!input)
            return data;


        rhi::ComPtr<rhi::IBuffer>  buffer;
        rhi::ComPtr<rhi::ITexture> texture;
        const bool                 isBuffer = SLANG_SUCCEEDED(
            resource->queryInterface(rhi::IBuffer::getTypeGuid(), reinterpret_cast<void**>(buffer.writeRef()))
        );
        if (isBuffer != (input->getType() == Field::Type::RawBuffer))
        {
            LOG_ERROR("resource kind mismatch at '{}'", name);
            return data;
        }
        auto& field = data.connectedResources.addField(*input);
        if (isBuffer)
        {
            const auto size = buffer->getDesc().size;
            if (!std::in_range<uint32_t>(size))
            {
                LOG_ERROR("buffer '{}' exceeds the render graph field size", name);
                return data;
            }
            field.rawBuffer(uint32_t(size));
        }
        else
        {
            CHECK(
                SLANG_SUCCEEDED(
                    resource->queryInterface(rhi::ITexture::getTypeGuid(), reinterpret_cast<void**>(texture.writeRef()))
                ),
                "resource '{}' must be a texture",
                name
            );
            const auto& desc   = texture->getDesc();
            const auto  format = desc.format;
            if (input->getFormat() != rhi::Format::Undefined && input->getFormat() != format)
            {
                LOG_ERROR("image format mismatch at '{}'", name);
                return data;
            }
            field.format(format).resourceType(
                resourceTypeToFieldType(desc.type),
                desc.size.width,
                desc.size.height,
                desc.size.depth,
                desc.sampleCount,
                desc.mipCount,
                desc.arrayLength
            );
        }
    }

    for (uint32_t index = 0; index < node.getOutgoingEdgeCount(); ++index)
    {
        const auto  edgeId     = node.getOutgoingEdge(index);
        const auto& connection = mGraph.mEdgeData.at(edgeId);
        if (connection.srcField.empty())
            continue;

        const auto targetId = mGraph.mpGraph->getEdge(edgeId)->getDestNode();
        const auto target   = std::ranges::find(mExecutionList, targetId, &PassData::index);
        if (target == mExecutionList.end())
            continue;
        const auto* output = getField(pass, connection.srcField, Visibility::Output);
        const auto* input  = getField(*target, connection.dstField, Visibility::Input);
        if (!output || !input)
            return data;
        if (canAutoResolve(*output, *input))
            continue;

        if (auto* field = data.connectedResources.getField(connection.srcField))
        {
            if (!is_set(field->getVisibility(), Visibility::Input))
            {
                field->merge(*input);
                if (logger::ScopedErrorLog::hasErrors())
                    return data;
            }
        }
        else
        {
            auto connected = *input;
            data.connectedResources.addField(
                connected.name(output->getName()).desc(output->getDesc()).visibility(output->getVisibility())
            );
        }
    }
    return data;
}

void RenderGraphCompiler::allocateResources(rhi::IDevice* device, ResourceCache* cache)
{
    for (const auto& pass : mExecutionList)
    {
        const auto& node          = *mGraph.mpGraph->getNode(pass.index);
        const auto  registerField = [&](const std::string& name, Field field, const std::string& alias = {})
        {
            const bool graphOutput = mGraph.isGraphOutput({pass.index, field.getName()});
            if (graphOutput && field.getBindFlags() != ResourceBindFlags::None)
                field.bindFlags(field.getBindFlags() | ResourceBindFlags::ShaderResource);
            cache->registerField(name, field, alias);
        };

        for (size_t fieldIndex = 0; fieldIndex < pass.reflector.getFieldCount(); ++fieldIndex)
        {
            const auto& field = *pass.reflector.getField(fieldIndex);
            const auto  name  = pass.name + '.' + field.getName();
            if (is_set(field.getVisibility(), Visibility::Input))
            {
                if (mDependencies.externalResources.contains(name))
                    registerField(name, field);
                continue;
            }

            if (is_set(field.getFlags(), Field::Flags::Optional) &&
                !mGraph.isGraphOutput({pass.index, field.getName()}))
            {
                bool connected = false;
                for (uint32_t outgoing = 0; outgoing < node.getOutgoingEdgeCount(); ++outgoing)
                {
                    if (mGraph.mEdgeData.at(node.getOutgoingEdge(outgoing)).srcField == field.getName())
                    {
                        connected = true;
                        break;
                    }
                }
                if (!connected)
                    continue;
            }

            registerField(name, field);
        }

        for (uint32_t incoming = 0; incoming < node.getIncomingEdgeCount(); ++incoming)
        {
            const auto  edgeId     = node.getIncomingEdge(incoming);
            const auto& connection = mGraph.mEdgeData.at(edgeId);
            if (connection.dstField.empty())
                continue;
            const auto  sourceId   = mGraph.mpGraph->getEdge(edgeId)->getSourceNode();
            const auto  sourceName = mGraph.mNodeData.at(sourceId).name + '.' + connection.srcField;
            const auto  targetName = pass.name + '.' + connection.dstField;
            const auto& field      = *pass.reflector.getField(connection.dstField);
            registerField(targetName, field, sourceName);
            if (logger::ScopedErrorLog::hasErrors())
                return;
        }
    }
    cache->allocateResources(device, mDependencies.defaultResourceProps);
}

void RenderGraphCompiler::restoreCompilationChanges()
{
    for (const auto& passName : mCompilationChanges.generatedPasses)
        mGraph.removePass(passName);
    for (const auto& [source, destination] : mCompilationChanges.removedEdges)
        mGraph.addEdge(source, destination);
    mCompilationChanges.generatedPasses.clear();
    mCompilationChanges.removedEdges.clear();
}
