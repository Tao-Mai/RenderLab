#include "render/graph/RenderGraph.h"

#include <slang-rhi.h>
#include "core/DirectedGraphTraversal.h"
#include "core/Logger.h"
#include "render/RenderContext.h"

#include <algorithm>
#include <tuple>
#include <utility>

RenderGraph::RenderGraph(rhi::ComPtr<rhi::IDevice> device, const std::string& name) :
    mpDevice(std::move(device)), mName(name), mpGraph(std::make_unique<DirectedGraph>())
{
}

RenderGraph::~RenderGraph()
{
    for (const auto& [index, node] : mNodeData)
        node.pPass->mPassChangedCB = [] {};
}

ref<RenderGraph> RenderGraph::create(rhi::ComPtr<rhi::IDevice> device, const std::string& name)
{
    return make_ref<RenderGraph>(std::move(device), name);
}

ref<RenderPass>
RenderGraph::createPass(const std::string& passName, const std::string& passType, const Properties& properties)
{
    auto pass = RenderPass::create(passType, mpDevice, properties);
    addPass(pass, passName);
    return pass;
}

namespace
{
    std::pair<std::string, std::string> parseFieldName(const std::string& name)
    {
        const auto dot = name.rfind('.');
        if (dot == std::string::npos)
            return {name, {}};

        CHECK(dot != 0 && dot + 1 < name.size(), "invalid resource endpoint '{}'", name);
        return {name.substr(0, dot), name.substr(dot + 1)};
    }
}  // namespace

RenderPass* RenderGraph::getRenderPassAndNamePair(
    bool input, const std::string& fullName, std::pair<std::string, std::string>& nameAndField
) const
{
    nameAndField              = parseFieldName(fullName);
    const auto& [name, field] = nameAndField;
    auto* pass                = getPass(name).get();
    if (!field.empty())
    {
        using Visibility       = RenderPassReflection::Visibility;
        const auto& defaults   = mCompilerDeps.defaultResourceProps;
        const auto  reflection = pass->reflect({defaults.dims, defaults.format});
        const auto* port       = reflection.getField(field);
        CHECK(
            port && is_set(port->visibility, input ? Visibility::Input : Visibility::Output),
            "unknown {} port '{}'",
            input ? "input" : "output",
            fullName
        );
    }
    return pass;
}

uint32_t RenderGraph::getPassIndex(const std::string& name) const
{
    const auto found = mNameToIndex.find(name);
    return found == mNameToIndex.end() ? kInvalidIndex : found->second;
}

uint32_t RenderGraph::addPass(const ref<RenderPass>& pass, const std::string& name)
{
    CHECK(pass);
    CHECK(!name.empty(), "invalid render pass instance name '{}'", name);
    CHECK(!doesPassExist(name), "duplicate render pass '{}'", name);

    const auto index = mpGraph->addNode();
    mNameToIndex.emplace(name, index);
    auto properties = nlohmann::json(pass->getProperties().toJson());
    mNodeData.emplace(index, NodeData{name, pass, properties, std::move(properties)});
    pass->mName          = name;
    pass->mPassChangedCB = [this] { mRecompile = true; };
    mRecompile = true;
    return index;
}

void RenderGraph::removePass(const std::string& name)
{
    const auto index = getPassIndex(name);
    CHECK(index != kInvalidIndex, "unknown render pass '{}'", name);

    mRecompile = true;
    for (const auto edgeId : mpGraph->removeNode(index))
        mEdgeData.erase(edgeId);
    std::erase_if(mOutputs, [index](const auto& output) { return output.nodeId == index; });
    const auto prefix = name + '.';
    std::erase_if(
        mCompilerDeps.externalResources,
        [&](const auto& input) { return input.first.rfind('.') == name.size() && input.first.starts_with(prefix); }
    );
    const auto& node = mNodeData.at(index);
    node.pPass->mPassChangedCB = [] {};
    mNodeData.erase(index);
    mNameToIndex.erase(name);
}

void RenderGraph::updatePass(const std::string& name, const Properties& properties)
{
    const auto index = getPassIndex(name);
    CHECK(index != kInvalidIndex, "unknown render pass '{}'", name);
    auto& node = mNodeData.at(index);
    auto  pass = RenderPass::create(node.pPass->getType(), mpDevice, properties);
    node.pPass->mPassChangedCB = [] {};
    node.pPass = pass;
    node.scripted = pass->getProperties().toJson();
    node.applied = node.scripted;
    pass->mName          = name;
    pass->mPassChangedCB = [this] { mRecompile = true; };
    mRecompile = true;
}

const ref<RenderPass>& RenderGraph::getPass(const std::string& name) const
{
    const auto index = getPassIndex(name);
    CHECK(index != kInvalidIndex, "unknown render pass '{}'", name);
    return mNodeData.at(index).pPass;
}

const nlohmann::json& RenderGraph::scriptedProperties(const std::string& name) const
{
    return mNodeData.at(getPassIndex(name)).scripted;
}

void RenderGraph::captureScriptedProperties()
{
    for (auto& [index, node] : mNodeData)
    {
        node.scripted = node.pPass->getProperties().toJson();
        node.applied = node.scripted;
    }
}

void RenderGraph::applyScriptedProperties(const RenderGraph& source)
{
    mName = source.mName;
    for (auto& [index, node] : mNodeData)
        node.scripted = source.scriptedProperties(node.name);
}

void RenderGraph::applyPassOverrides(const nlohmann::json& overrides)
{
    for (const auto& [type, instances] : overrides.items())
    {
        CHECK(instances.is_object(), "options for pass type '{}' must be an object", type);
        for (const auto& [name, values] : instances.items())
            CHECK(values.is_object(), "options for pass '{}' must be an object", name);
    }

    for (auto& [index, node] : mNodeData)
    {
        auto values = node.scripted;
        if (const auto byType = overrides.find(node.pPass->getType()); byType != overrides.end())
            if (const auto byName = byType->find(node.name); byName != byType->end())
                values.update(*byName, true);
        if (values == node.applied)
            continue;
        if (values != nlohmann::json(node.pPass->getProperties().toJson()))
            node.pPass->setProperties(Properties::json(values));
        node.applied = std::move(values);
    }
}

uint32_t RenderGraph::getEdge(const std::string& source, const std::string& target)
{
    const auto [srcPass, srcField] = parseFieldName(source);
    const auto [dstPass, dstField] = parseFieldName(target);
    const auto sourceId            = getPassIndex(srcPass);
    const auto targetId            = getPassIndex(dstPass);
    if (sourceId == kInvalidIndex || targetId == kInvalidIndex)
        return kInvalidIndex;

    const auto& node = *mpGraph->getNode(sourceId);
    for (uint32_t index = 0; index < node.getOutgoingEdgeCount(); ++index)
    {
        const auto  edgeId = node.getOutgoingEdge(index);
        const auto& data   = mEdgeData.at(edgeId);
        if (mpGraph->getEdge(edgeId)->getDestNode() == targetId && data.srcField == srcField &&
            data.dstField == dstField)
            return edgeId;
    }
    return kInvalidIndex;
}

uint32_t RenderGraph::addEdge(const std::string& source, const std::string& target)
{
    std::pair<std::string, std::string> src;
    std::pair<std::string, std::string> dst;
    getRenderPassAndNamePair(false, source, src);
    getRenderPassAndNamePair(true, target, dst);
    auto& [srcPass, srcField] = src;
    auto& [dstPass, dstField] = dst;
    const auto sourceId       = getPassIndex(srcPass);
    const auto targetId       = getPassIndex(dstPass);
    CHECK(sourceId != targetId && (srcField.empty() == dstField.empty()), "invalid edge '{}' -> '{}'", source, target);
    if (!dstField.empty())
    {
        const auto& node = *mpGraph->getNode(targetId);
        for (uint32_t index = 0; index < node.getIncomingEdgeCount(); ++index)
            CHECK(
                mEdgeData.at(node.getIncomingEdge(index)).dstField != dstField,
                "input '{}' already has an edge",
                target
            );
    }
    CHECK(
        !DirectedGraphPathDetector::hasPath(*mpGraph, targetId, sourceId),
        "edge '{}' -> '{}' introduces a cycle",
        source,
        target
    );

    const auto edgeId = mpGraph->addEdge(sourceId, targetId);
    mEdgeData.emplace(edgeId, EdgeData{std::move(srcField), std::move(dstField)});
    mRecompile = true;
    return edgeId;
}

void RenderGraph::removeEdge(const std::string& source, const std::string& target)
{
    const auto edgeId = getEdge(source, target);
    CHECK(edgeId != kInvalidIndex, "unknown edge '{}' -> '{}'", source, target);
    removeEdge(edgeId);
}

void RenderGraph::removeEdge(uint32_t edgeId)
{
    CHECK(mpGraph->doesEdgeExist(edgeId), "unknown edge {}", edgeId);
    mpGraph->removeEdge(edgeId);
    mEdgeData.erase(edgeId);
    mRecompile = true;
}

void RenderGraph::setInput(const std::string& name, rhi::IResource* resource)
{
    auto&      inputs  = mCompilerDeps.externalResources;
    const auto current = inputs.find(name);
    if (!resource)
    {
        CHECK(current != inputs.end(), "input '{}' is not registered", name);
        inputs.erase(current);
    }
    else
    {
        if (current == inputs.end())
        {
            std::pair<std::string, std::string> input;
            getRenderPassAndNamePair(true, name, input);
            CHECK(!input.second.empty(), "invalid input '{}'", name);
        }
        inputs.insert_or_assign(name, resource);
    }
    if (mpExe)
        mpExe->setInput(name, resource);
}

void RenderGraph::markOutput(const std::string& name)
{
    if (name == "*")
    {
        for (const auto& output : getAvailableOutputs())
            markOutput(output);
        return;
    }

    std::pair<std::string, std::string> endpoint;
    getRenderPassAndNamePair(false, name, endpoint);
    CHECK(!endpoint.second.empty(), "invalid render graph output '{}'", name);
    GraphOut   output{getPassIndex(endpoint.first), std::move(endpoint.second)};
    const auto found = std::ranges::find(mOutputs, output);
    if (found == mOutputs.end())
    {
        mOutputs.push_back(std::move(output));
        mRecompile = true;
    }
}

void RenderGraph::unmarkOutput(const std::string& name)
{
    const auto [pass, field] = parseFieldName(name);
    const auto node          = getPassIndex(pass);
    CHECK(node != kInvalidIndex && !field.empty(), "invalid render graph output '{}'", name);
    const auto found = std::ranges::find(mOutputs, GraphOut{node, field});
    if (found != mOutputs.end())
    {
        mOutputs.erase(found);
        mRecompile = true;
    }
}

bool RenderGraph::isGraphOutput(const std::string& name) const
{
    const auto [pass, field] = parseFieldName(name);
    return isGraphOutput(GraphOut{getPassIndex(pass), field});
}

bool RenderGraph::isGraphOutput(const GraphOut& output) const
{
    return std::ranges::find(mOutputs, output) != mOutputs.end();
}

rhi::ComPtr<rhi::IResource> RenderGraph::getOutput(const std::string& name)
{
    CHECK(mpExe && !mRecompile, "compile the graph before fetching '{}'", name);
    CHECK(isGraphOutput(name), "'{}' is not a marked graph output", name);
    return mpExe->getResource(name);
}

rhi::ComPtr<rhi::IResource> RenderGraph::getOutput(uint32_t index)
{
    CHECK(mpExe && !mRecompile && index < mOutputs.size());
    return mpExe->getOutput(index);
}

std::string RenderGraph::getOutputName(size_t index) const
{

    const auto& output = mOutputs[index];
    return mNodeData.at(output.nodeId).name + '.' + output.field;
}

std::vector<std::string> RenderGraph::getAvailableOutputs() const
{
    std::vector<std::string> names;
    const auto&              defaults = mCompilerDeps.defaultResourceProps;
    for (uint32_t node = 0; node < mpGraph->getCurrentNodeId(); ++node)
    {
        if (!mpGraph->doesNodeExist(node))
            continue;
        const auto& data       = mNodeData.at(node);
        const auto  reflection = data.pPass->reflect({defaults.dims, defaults.format});
        for (const auto& field : reflection.fields())
            if (is_set(field.visibility, RenderPassReflection::Visibility::Output))
                names.push_back(data.name + '.' + field.name);
    }
    return names;
}

bool RenderGraph::hasSameTopology(const RenderGraph& other) const
{
    if (mNodeData.size() != other.mNodeData.size() || mEdgeData.size() != other.mEdgeData.size() ||
        mOutputs.size() != other.mOutputs.size())
        return false;

    for (const auto& [name, index] : mNameToIndex)
    {
        const auto otherIndex = other.getPassIndex(name);
        if (otherIndex == kInvalidIndex ||
            mNodeData.at(index).pPass->getType() != other.mNodeData.at(otherIndex).pPass->getType())
            return false;
    }

    for (size_t index = 0; index < mOutputs.size(); ++index)
    {
        const auto& output     = mOutputs[index];
        const auto& nextOutput = other.mOutputs[index];
        if (output.field != nextOutput.field ||
            mNodeData.at(output.nodeId).name != other.mNodeData.at(nextOutput.nodeId).name)
            return false;
    }

    const auto edgeEndpoints = [](const RenderGraph& graph)
    {
        std::vector<std::tuple<std::string_view, std::string_view, std::string_view, std::string_view>> endpoints;
        endpoints.reserve(graph.mEdgeData.size());
        for (const auto& [id, data] : graph.mEdgeData)
        {
            const auto& edge = *graph.mpGraph->getEdge(id);
            endpoints.emplace_back(
                graph.mNodeData.at(edge.getSourceNode()).name,
                data.srcField,
                graph.mNodeData.at(edge.getDestNode()).name,
                data.dstField
            );
        }
        std::ranges::sort(endpoints);
        return endpoints;
    };
    return edgeEndpoints(*this) == edgeEndpoints(other);
}

void RenderGraph::onResize(Extent2D extent, rhi::Format format)
{
    mCompilerDeps.defaultResourceProps = {{extent.width, extent.height}, format};
    mRecompile                         = true;
}

bool RenderGraph::compile(RenderContext* context, std::string& log)
{

    log.clear();
    if (!mpExe || mRecompile)
    {
        mpExe.reset();

        const logger::ScopedErrorLog errors(log);
        mpExe = RenderGraphCompiler::compile(*this, context, mCompilerDeps);
        if (!mpExe)
            return false;
        mRecompile = false;
    }
    return true;
}

void RenderGraph::execute(RenderContext* context)
{
    mpExe->execute(context);
}

void RenderGraph::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    if (mpExe)
        mpExe->renderUI(context, widget);
}

namespace
{
template <class T>
rhi::ComPtr<T> queryResource(rhi::IResource* resource)
{
    rhi::ComPtr<T> typed;
    if (resource &&
        SLANG_SUCCEEDED(resource->queryInterface(T::getTypeGuid(), reinterpret_cast<void**>(typed.writeRef()))))
        return typed;
    return {};
}
}  // namespace

rhi::ComPtr<rhi::ITexture> RenderGraph::getOutputTexture(const std::string& name)
{
    return queryResource<rhi::ITexture>(getOutput(name));
}
rhi::ComPtr<rhi::ITexture> RenderGraph::getOutputTexture(uint32_t index)
{
    return queryResource<rhi::ITexture>(getOutput(index));
}
rhi::ComPtr<rhi::IBuffer> RenderGraph::getOutputBuffer(const std::string& name)
{
    return queryResource<rhi::IBuffer>(getOutput(name));
}
