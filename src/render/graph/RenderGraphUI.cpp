/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "render/graph/RenderGraphUI.h"

#include "core/DirectedGraphTraversal.h"
#include "core/Logger.h"
#include "core/Scripting.h"
#include "editor/PassEditor.h"

#include <imgui.h>
#include <imguinodegrapheditor/imguinodegrapheditor.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <string_view>
#include <utility>

namespace Falcor
{
namespace
{
using Field = RenderPassReflection::Field;
using Visibility = Field::Visibility;

constexpr float kUpdateTimeInterval = 2.0f;
constexpr float kTimeTillPopup = 2.0f;
constexpr float2 kNodeSpacing{384.0f, 128.0f};
constexpr uint32_t kEdgesColor = IM_COL32(210, 210, 210, 255);
constexpr uint32_t kGraphOutputsColor = IM_COL32(100, 185, 116, 255);
constexpr uint32_t kExecutionEdgeColor = IM_COL32(242, 177, 64, 255);
constexpr char kInPrefix[] = "##inEx_ ";
constexpr char kOutPrefix[] = "##outEx_";
constexpr char kRenderPassType[] = "RenderPassType";
constexpr char kPinMenu[] = "PinMenu";
} // namespace

class RenderGraphUI::RenderGraphNode : public ImGui::Node
{
public:
    void initialize(
        const std::string& name,
        const std::string& outputsString,
        const std::string& inputsString,
        uint32_t guiNodeID,
        RenderPass* pass
    )
    {
        init(name.c_str(), Pos, inputsString.c_str(), outputsString.c_str(), static_cast<int>(guiNodeID));
        const auto color = Gui::pickUniqueColor(pass->getType());
        overrideTitleBgColor = ImGui::GetColorU32({color.x, color.y, color.z, color.w});
    }

    static RenderGraphNode* create(const ImVec2& position)
    {
        auto* node = static_cast<RenderGraphNode*>(ImGui::MemAlloc(sizeof(RenderGraphNode)));
        IM_PLACEMENT_NEW(node) RenderGraphNode();
        node->fields.addFieldCustom(renderUI, nullptr, node);
        node->Pos = position;
        return node;
    }

    float2 getPos() { return {Pos.x, Pos.y}; }

    void setPinColor(uint32_t color, uint32_t index, bool isInput = false)
    {
        if (isInput)
            inputColors[index] = color;
        else
            outputColors[index] = color;
    }

private:
    static bool renderUI(ImGui::FieldInfo& field);
};

class RenderGraphUI::NodeGraphEditorGui : public ImGui::NodeGraphEditor
{
public:
    explicit NodeGraphEditorGui(RenderGraphUI* pRenderGraphUI) : mpRenderGraphUI(pRenderGraphUI)
    {
        show_left_pane = false;
        show_top_pane = false;
        show_node_copy_paste_buttons = false;
        allowOnlyOneLinkPerInputSlot = false;
    }

    void reset()
    {
        const auto previousScroll = scrolling;
        setNodeCallback(nullptr);
        setLinkCallback(nullptr);
        selectedLink = RenderGraph::kInvalidIndex;
        setPopupNode(nullptr);
        menuNode = nullptr;
        dragNode.reset();
        clear();
        scrolling = previousScroll;
        mpIDtoNode.clear();
    }

    ImGui::Node* addAndInitNode(decltype(mRenderPassUI)::value_type& entry, RenderPass* pass, const ImVec2& position)
    {
        const auto& [name, passUI] = entry;
        const auto labels = [](const auto& pins)
        {
            std::string result;
            for (const auto& pin : pins)
            {
                if (!result.empty())
                    result += ';';
                const std::string_view name = pin.mPinName;
                result += name.starts_with('#') ? "Execution" : name;
            }
            return result;
        };

        auto* node = RenderGraphNode::create(position);
        node->initialize(name, labels(passUI.mOutputPins), labels(passUI.mInputPins), passUI.mGuiNodeID, pass);
        node->user_ptr = &entry;
        for (const auto& pin : passUI.mOutputPins)
            if (pin.mIsGraphOutput)
                node->setPinColor(kGraphOutputsColor, pin.mGuiPinID);

        // The canvas uses graph node IDs without offering them as node creation menu entries.
        const auto typeCount = static_cast<int>(passUI.mGuiNodeID) + 1;
        availableNodesInfoInverseMap.resize(std::max(availableNodesInfoInverseMap.size(), typeCount), -1);
        mpIDtoNode.emplace(passUI.mGuiNodeID, node);
        return addNode(node);
    }

    ImGui::Node*& getNodeFromID(uint32_t nodeID) { return mpIDtoNode.at(nodeID); }

    uint32_t addLinkFromGraph(
        ImGui::Node* inputNode,
        int inputSlot,
        ImGui::Node* outputNode,
        int outputSlot,
        bool checkIfAlreadyPresent = false,
        ImU32 color = kEdgesColor
    )
    {
        const auto previous = std::exchange(linkCallback, setLinkFromGraph);
        const bool inserted = addLink(inputNode, inputSlot, outputNode, outputSlot, checkIfAlreadyPresent, color);
        linkCallback = previous;
        return inserted ? static_cast<uint32_t>(links.size() - 1) : RenderGraph::kInvalidIndex;
    }

    float2 getOffsetPos() const { return {offset.x, offset.y}; }
    ImGui::NodeLink& getLink(int32_t index)
    {
        DCHECK(index >= 0 && index < links.size());
        return links[index];
    }

    void setPopupNode(ImGui::Node* node)
    {
        if (mpFocusedNode != node)
        {
            mpFocusedNode = node;
            mPinIndexToDisplay = RenderGraph::kInvalidIndex;
            mPopupPinHoverTime = 0.0f;
        }
    }

    ImGui::Node* getPopupNode() { return mpFocusedNode; }

    void setPopupPin(uint32_t pinIndex, bool isInput)
    {
        const auto now = ImGui::GetTime();
        if (pinIndex != mPinIndexToDisplay || isInput != mPopupPinIsInput || now - mLastTime >= kTimeTillPopup)
            mPopupPinHoverTime = 0.0f;

        mPinIndexToDisplay = pinIndex;
        mPopupPinIsInput = isInput;
        mPopupPinHoverTime += ImGui::GetIO().DeltaTime;
        mLastTime = now;
    }

    void deselectPopupPin()
    {
        if (ImGui::GetTime() - mLastTime >= kTimeTillPopup)
            setPopupNode(nullptr);
    }

    uint32_t getPopupPinIndex() const
    {
        return mPopupPinHoverTime >= kTimeTillPopup ? mPinIndexToDisplay : RenderGraph::kInvalidIndex;
    }
    bool isPopupPinInput() const { return mPopupPinIsInput; }

    static void setNode(ImGui::Node*& node, NodeState state, ImGui::NodeGraphEditor& editor)
    {
        if (state == NS_DELETED)
        {
            auto& gui = static_cast<NodeGraphEditorGui&>(editor);
            if (gui.getPopupNode() == node)
                gui.setPopupNode(nullptr);
            gui.mpRenderGraphUI->removeRenderPass(static_cast<decltype(mRenderPassUI)::value_type*>(node->user_ptr)->first);
        }
    }

    static void setLinkFromGui(ImGui::NodeLink& link, LinkState state, ImGui::NodeGraphEditor& editor)
    {
        auto& gui = static_cast<NodeGraphEditorGui&>(editor);
        auto& graphUI = *gui.mpRenderGraphUI;
        const auto& [srcName, source] = *static_cast<decltype(mRenderPassUI)::value_type*>(link.InputNode->user_ptr);
        const auto& [dstName, target] = *static_cast<decltype(mRenderPassUI)::value_type*>(link.OutputNode->user_ptr);
        const auto& srcField = source.mOutputPins[link.InputSlot].mPinName;
        const auto& dstField = target.mInputPins[link.OutputSlot].mPinName;
        if (state == LS_DELETED)
        {
            gui.selectedLink = RenderGraph::kInvalidIndex;
            graphUI.removeEdge(srcName, dstName, srcField, dstField);
        }
        else if (!graphUI.addLink(srcName, dstName, srcField, dstField, link.LinkColor))
        {
            gui.selectedLink = RenderGraph::kInvalidIndex;
            editor.setLinkCallback(nullptr);
            editor.removeLink(link.InputNode, link.InputSlot, link.OutputNode, link.OutputSlot);
            editor.setLinkCallback(setLinkFromGui);
        }
    }

    static void setLinkFromGraph(ImGui::NodeLink& link, LinkState state, ImGui::NodeGraphEditor&)
    {
        DCHECK(state == LS_ADDED);
        const auto& [srcName, source] = *static_cast<decltype(mRenderPassUI)::value_type*>(link.InputNode->user_ptr);
        auto& target = static_cast<decltype(mRenderPassUI)::value_type*>(link.OutputNode->user_ptr)->second;
        auto& pin = target.mInputPins[link.OutputSlot];
        pin.mConnectedPinName = source.mOutputPins[link.InputSlot].mPinName;
        pin.mConnectedNodeName = srcName;
    }

    ~NodeGraphEditorGui() override { reset(); }

private:
    friend class RenderGraphUI;
    friend class RenderGraphNode;

    RenderGraphUI* mpRenderGraphUI;
    float mPopupPinHoverTime = 0.0f;
    double mLastTime = 0.0;
    ImGui::Node* mpFocusedNode = nullptr;
    uint32_t mPinIndexToDisplay = RenderGraph::kInvalidIndex;
    bool mPopupPinIsInput = false;
    std::unordered_map<uint32_t, ImGui::Node*> mpIDtoNode;
};

bool RenderGraphUI::RenderGraphNode::renderUI(ImGui::FieldInfo& field)
{
    auto& node = *static_cast<RenderGraphNode*>(field.userData);
    ImGui::Dummy({ImGui::CalcItemWidth(), ImGui::GetTextLineHeight() * std::max(node.InputsCount, node.OutputsCount)});
    if (ImGui::IsAnyMouseDown() || !ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
        return false;

    auto& editor = static_cast<NodeGraphEditorGui&>(node.getNodeGraphEditor());
    const auto offset = editor.getOffsetPos();
    const auto scale = editor.oldFontWindowScale;
    const auto radius = editor.GetStyle().node_slots_radius * scale;
    for (const bool isInput : {true, false})
    {
        const int count = isInput ? node.InputsCount : node.OutputsCount;
        for (int index = 0; index < count; ++index)
        {
            const auto position = isInput ? node.GetInputSlotPos(index, scale) : node.GetOutputSlotPos(index, scale);
            const float x = offset.x + position.x;
            const float y = offset.y + position.y;
            if (ImGui::IsMouseHoveringRect({x - radius, y - radius}, {x + radius, y + radius}))
            {
                editor.setPopupNode(&node);
                editor.setPopupPin(static_cast<uint32_t>(index), isInput);
                return false;
            }
        }
    }
    return false;
}

void RenderPassUI::addUIPin(
    const std::string& fieldName,
    uint32_t guiPinID,
    bool isInput,
    const std::string& connectedPinName,
    const std::string& connectedNodeName,
    bool isGraphOutput
)
{
    auto& pins = isInput ? mInputPins : mOutputPins;
    auto& indices = isInput ? mNameToIndexInput : mNameToIndexOutput;
    if (pins.size() <= guiPinID)
        pins.resize(guiPinID + 1);

    pins[guiPinID] = {fieldName, guiPinID, connectedPinName, connectedNodeName, isGraphOutput};
    indices.insert_or_assign(fieldName, guiPinID);
}

void RenderPassUI::renderPinUI(const std::string& passName, RenderGraphUI* pGraphUI, uint32_t index, bool input)
{
    auto& pin = (input ? mInputPins : mOutputPins)[index];
    if (const auto* field = mReflection.getField(pin.mPinName))
        pin.renderUI(*field, pGraphUI, passName);
}

void RenderPassUI::PinUI::renderFieldInfo(
    const Field& field, RenderGraphUI*, const std::string&, const std::string& fieldName
)
{
    ImGui::TextUnformatted(fieldName.c_str());
    if (!field.getDesc().empty())
        ImGui::TextWrapped("%s", field.getDesc().c_str());
    ImGui::Text("%s", to_string(field.getType()).c_str());
    if (field.getFormat() != ResourceFormat::Unknown)
        ImGui::Text("%s", to_string(field.getFormat()).c_str());
    if (field.getWidth())
        ImGui::Text("%u x %u x %u", field.getWidth(), field.getHeight(), field.getDepth());
    if (field.getSampleCount() > 1)
        ImGui::Text("%u samples", field.getSampleCount());
}

void RenderPassUI::PinUI::renderUI(const Field& field, RenderGraphUI* graphUI, const std::string& passName)
{
    renderFieldInfo(field, graphUI, passName, mPinName);
    if (!mConnectedNodeName.empty())
        ImGui::Text("%s.%s", mConnectedNodeName.c_str(), mConnectedPinName.c_str());
    if (is_set(field.getVisibility(), Visibility::Output))
    {
        bool marked = mIsGraphOutput;
        if (ImGui::Checkbox("Graph output", &marked))
        {
            if (marked)
                graphUI->addOutput(passName, mPinName);
            else
                graphUI->removeOutput(passName, mPinName);
        }
    }
}

RenderGraphUI::RenderGraphUI() = default;

RenderGraphUI::RenderGraphUI(const ref<RenderGraph>& pGraph, const std::string& graphName) :
    mpRenderGraph(pGraph), mpIr(std::make_shared<RenderGraphIR>(graphName, false)), mRenderGraphName(graphName)
{
    DCHECK(mpRenderGraph);
}

RenderGraphUI::~RenderGraphUI() = default;

void RenderGraphUI::addRenderPass(const std::string& name, const std::string& nodeTypeName)
{
    auto& plugins = PluginManager::instance();
    if (name.empty() || mpRenderGraph->doesPassExist(name) || !plugins.hasClass<RenderPass>(nodeTypeName))
    {
        mLogString += std::format("Cannot create pass '{}' of type '{}'\n", name, nodeTypeName);
        return;
    }

    mpIr->createPass(nodeTypeName, name);
    mShouldUpdate = true;
}

void RenderGraphUI::removeRenderPass(const std::string& name)
{
    if (!mpRenderGraph->doesPassExist(name))
    {
        mLogString += std::format("Unknown pass '{}'\n", name);
        return;
    }
    mpIr->removePass(name);
    mShouldUpdate = true;
}

void RenderGraphUI::addOutput(const std::string& outputParam)
{
    const auto available = mpRenderGraph->getAvailableOutputs();
    if (std::ranges::find(available, outputParam) == available.end())
    {
        mLogString += std::format("Unknown graph output '{}'\n", outputParam);
        return;
    }
    if (!mpRenderGraph->isGraphOutput(outputParam))
    {
        mpIr->markOutput(outputParam);
        mShouldUpdate = true;
    }
}

void RenderGraphUI::addOutput(const std::string& outputPass, const std::string& outputField)
{
    addOutput(outputPass + '.' + outputField);
}

void RenderGraphUI::removeOutput(const std::string& outputPass, const std::string& outputField)
{
    mpIr->unmarkOutput(outputPass + '.' + outputField);
    mShouldUpdate = true;
}

bool RenderGraphUI::addLink(
    const std::string& srcPass,
    const std::string& dstPass,
    const std::string& srcField,
    const std::string& dstField,
    uint32_t& color
)
{
    const bool execution = srcField.starts_with('#');
    const auto source = execution ? srcPass : srcPass + '.' + srcField;
    const auto destination = execution ? dstPass : dstPass + '.' + dstField;
    const auto srcIndex = mpRenderGraph->getPassIndex(srcPass);
    const auto dstIndex = mpRenderGraph->getPassIndex(dstPass);
    const auto reject = [&]
    {
        mLogString += std::format("Cannot connect '{}' to '{}'\n", source, destination);
        return false;
    };
    if (srcField.empty() || dstField.empty() || srcIndex == RenderGraph::kInvalidIndex ||
        dstIndex == RenderGraph::kInvalidIndex || execution != dstField.starts_with('#') || srcIndex == dstIndex ||
        mpRenderGraph->getEdge(source, destination) != RenderGraph::kInvalidIndex ||
        DirectedGraphPathDetector::hasPath(*mpRenderGraph->mpGraph, dstIndex, srcIndex))
        return reject();

    if (execution)
    {
        if (srcField != kOutPrefix + srcPass || dstField != kInPrefix + dstPass)
            return reject();
    }
    else
    {
        const auto& node = *mpRenderGraph->mpGraph->getNode(dstIndex);
        for (uint32_t index = 0; index < node.getIncomingEdgeCount(); ++index)
            if (mpRenderGraph->mEdgeData.at(node.getIncomingEdge(index)).dstField == dstField)
                return reject();
        if (mpRenderGraph->mCompilerDeps.externalResources.contains(destination))
            return reject();

        const auto& defaults = mpRenderGraph->mCompilerDeps.defaultResourceProps;
        RenderPass::CompileData compileData{defaults.dims, defaults.format};
        const auto sourceReflection = mpRenderGraph->getPass(srcPass)->reflect(compileData);
        const auto initial = mpRenderGraph->getPass(dstPass)->reflect(compileData);
        const auto* output = sourceReflection.getField(srcField);
        const auto* initialInput = initial.getField(dstField);
        if (!output || !is_set(output->getVisibility(), Visibility::Output) ||
            !initialInput || !is_set(initialInput->getVisibility(), Visibility::Input) ||
            ((output->getType() == Field::Type::RawBuffer) != (initialInput->getType() == Field::Type::RawBuffer)))
            return reject();

        auto connected = *output;
        compileData.connectedResources.addField(connected.name(dstField).visibility(Visibility::Input));
        const auto destinationReflection = mpRenderGraph->getPass(dstPass)->reflect(compileData);
        const auto* input = destinationReflection.getField(dstField);
        const auto matches = [](auto left, auto right) { return left == decltype(left){} || right == decltype(right){} || left == right; };
        if (!input || !is_set(input->getVisibility(), Visibility::Input) || input->getType() != output->getType() ||
            !matches(input->getWidth(), output->getWidth()) ||
            !matches(input->getHeight(), output->getHeight()) ||
            !matches(input->getDepth(), output->getDepth()) ||
            !matches(input->getArraySize(), output->getArraySize()) ||
            !matches(input->getMipCount(), output->getMipCount()) ||
            (!matches(input->getSampleCount(), output->getSampleCount()) &&
             !(input->getSampleCount() == 1 && output->getSampleCount() > 1)) ||
            !matches(input->getFormat(), output->getFormat()))
            return reject();
    }

    mpIr->addEdge(source, destination);
    color = execution ? kExecutionEdgeColor : kEdgesColor;
    mShouldUpdate = true;
    return true;
}

void RenderGraphUI::removeEdge(
    const std::string& srcPass, const std::string& dstPass, const std::string& srcField, const std::string& dstField
)
{
    DCHECK(!srcField.empty() && !dstField.empty());
    const bool execution = srcField.starts_with('#');
    DCHECK(execution == dstField.starts_with('#'));
    const auto source = execution ? srcPass : srcPass + '.' + srcField;
    const auto destination = execution ? dstPass : dstPass + '.' + dstField;
    // Deleting a node can report removal of a link that was rejected when it was added.
    if (mpRenderGraph->getEdge(source, destination) != RenderGraph::kInvalidIndex)
    {
        mpIr->removeEdge(source, destination);
        mShouldUpdate = true;
    }
}

void RenderGraphUI::updateGraph(RenderContext* pRenderContext)
{
    if (!mShouldUpdate)
        return;

    auto commands = mpIr->getIR();
    Scripting::Context script;
    script.setObject("g", mpRenderGraph);
    Scripting::runScript(commands, script);

    mLogString += commands;
    if (mRecordUpdates)
        mUpdateCommands += commands;
    std::string log;
    if (!mpRenderGraph->compile(pRenderContext, log))
        mLogString += log;

    mpIr = std::make_shared<RenderGraphIR>(mRenderGraphName, false);
    mShouldUpdate = false;
    mRebuildDisplayData = true;
}

void RenderGraphUI::writeUpdateScriptToFile(RenderContext* pRenderContext, const std::filesystem::path& filePath, float lastFrameTimes)
{
    mTimeSinceLastUpdate += lastFrameTimes;
    if (mTimeSinceLastUpdate < kUpdateTimeInterval || mUpdateCommands.empty() || !mpRenderGraph->compile(pRenderContext))
        return;

    std::ofstream file(filePath);
    file << mUpdateCommands;
    file.close();
    CHECK(file, "could not write render graph updates to '{}'", filePath.string());
    mUpdateCommands.clear();
    mTimeSinceLastUpdate = 0.0f;
}

void RenderGraphUI::reset()
{
    if (mpNodeGraphEditor)
        mpNodeGraphEditor->reset();
    mRebuildDisplayData = true;
}

void RenderGraphUI::setRecordUpdates(bool recordUpdates) { mRecordUpdates = recordUpdates; }

float2 RenderGraphUI::getNextNodePosition(uint32_t nodeID)
{
    auto position = mNewNodeStartPosition;
    // updateDisplayData adds the nodes in topological order.
    position.x += kNodeSpacing.x * (mpNodeGraphEditor->getNumNodes() + 1);

    const auto& graph = *mpRenderGraph->mpGraph;
    const auto& node = *graph.getNode(nodeID);
    const auto incomingCount = node.getIncomingEdgeCount();
    for (uint32_t index = 0; index < incomingCount; ++index)
    {
        const auto sourceID = graph.getEdge(node.getIncomingEdge(index))->getSourceNode();
        const auto outgoingCount = graph.getNode(sourceID)->getOutgoingEdgeCount();
        if (outgoingCount > incomingCount)
        {
            position.y += kNodeSpacing.y * (outgoingCount - incomingCount);
            break;
        }
    }
    return position;
}

void RenderGraphUI::updateDisplayData(RenderContext* pRenderContext)
{
    mpRenderGraph->compile(pRenderContext);

    std::unordered_map<std::string, std::pair<float2, bool>> positions;
    for (int index = 0; index < mpNodeGraphEditor->getNumNodes(); ++index)
    {
        auto* node = static_cast<RenderGraphNode*>(mpNodeGraphEditor->getNode(index));
        const auto& name = static_cast<decltype(mRenderPassUI)::value_type*>(node->user_ptr)->first;
        positions.emplace(name, std::pair{node->getPos(), mpNodeGraphEditor->isNodeSelected(node)});
    }
    // Nodes keep pointers to their pass UI entries.
    mpNodeGraphEditor->reset();
    mRenderPassUI.clear();

    const auto& defaults = mpRenderGraph->mCompilerDeps.defaultResourceProps;
    for (const auto id : DirectedGraphTopologicalSort::sort(*mpRenderGraph->mpGraph))
    {
        const auto& [name, pass] = mpRenderGraph->mNodeData.at(id);
        auto& entry = *mRenderPassUI.try_emplace(name).first;
        auto& passUI = entry.second;
        passUI.mGuiNodeID = static_cast<uint32_t>(mpNodeGraphEditor->getNumNodes());
        passUI.mReflection = pass->reflect({defaults.dims, defaults.format});
        for (size_t index = 0; index < passUI.mReflection.getFieldCount(); ++index)
        {
            const auto& field = *passUI.mReflection.getField(index);
            const auto& fieldName = field.getName();
            if (is_set(field.getVisibility(), Visibility::Input))
                passUI.addUIPin(fieldName, static_cast<uint32_t>(passUI.mInputPins.size()), true);
            if (is_set(field.getVisibility(), Visibility::Output))
                passUI.addUIPin(fieldName, static_cast<uint32_t>(passUI.mOutputPins.size()), false, {}, {},
                                mpRenderGraph->isGraphOutput(name + '.' + fieldName));
        }
        passUI.addUIPin(kInPrefix + name, static_cast<uint32_t>(passUI.mInputPins.size()), true);
        passUI.addUIPin(kOutPrefix + name, static_cast<uint32_t>(passUI.mOutputPins.size()), false);
        CHECK(passUI.mInputPins.size() <= IMGUINODE_MAX_INPUT_SLOTS, "pass '{}' has too many input pins", name);
        CHECK(passUI.mOutputPins.size() <= IMGUINODE_MAX_OUTPUT_SLOTS, "pass '{}' has too many output pins", name);

        const auto previous = positions.find(name);
        const auto position = previous != positions.end() ? previous->second.first : getNextNodePosition(id);
        auto* node = mpNodeGraphEditor->addAndInitNode(entry, pass.get(), {position.x, position.y});
        if (previous != positions.end() && previous->second.second)
            mpNodeGraphEditor->selectNode(node);
    }

    updatePins();
    mpNodeGraphEditor->setNodeCallback(NodeGraphEditorGui::setNode);
    mpNodeGraphEditor->setLinkCallback(NodeGraphEditorGui::setLinkFromGui);
    mRebuildDisplayData = false;
}

void RenderGraphUI::updatePins(bool addLinks)
{
    for (const auto& [id, data] : mpRenderGraph->mEdgeData)
    {
        const auto& edge = *mpRenderGraph->mpGraph->getEdge(id);
        const auto& src = mpRenderGraph->mNodeData.at(edge.getSourceNode()).name;
        const auto& dst = mpRenderGraph->mNodeData.at(edge.getDestNode()).name;
        auto& source = mRenderPassUI.at(src);
        auto& target = mRenderPassUI.at(dst);
        const bool execution = data.srcField.empty();
        const auto output = source.mNameToIndexOutput.find(execution ? kOutPrefix + src : data.srcField);
        const auto input = target.mNameToIndexInput.find(execution ? kInPrefix + dst : data.dstField);
        if (input == target.mNameToIndexInput.end() || output == source.mNameToIndexOutput.end())
            continue;

        auto* inputNode = mpNodeGraphEditor->getNodeFromID(source.mGuiNodeID);
        auto* outputNode = mpNodeGraphEditor->getNodeFromID(target.mGuiNodeID);
        const auto inputSlot = static_cast<int>(output->second);
        const auto outputSlot = static_cast<int>(input->second);
        const auto color = execution ? kExecutionEdgeColor : kEdgesColor;
        if (addLinks)
            mpNodeGraphEditor->addLinkFromGraph(inputNode, inputSlot, outputNode, outputSlot, false, color);
        else
        {
            ImGui::NodeLink link(inputNode, inputSlot, outputNode, outputSlot, color);
            NodeGraphEditorGui::setLinkFromGraph(link, ImGui::NodeGraphEditor::LS_ADDED, *mpNodeGraphEditor);
        }
    }
}

void RenderGraphUI::renderPopupMenu()
{
    auto& editor = *mpNodeGraphEditor;
    const bool isPopupOpen = ImGui::IsPopupOpen(kPinMenu);
    if (!isPopupOpen)
    {
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            editor.selectedLink = RenderGraph::kInvalidIndex;
        if (editor.mLastTime != ImGui::GetTime())
            editor.setPopupNode(nullptr);
        if (editor.getPopupPinIndex() == RenderGraph::kInvalidIndex && editor.selectedLink == RenderGraph::kInvalidIndex)
            return;
        ImGui::OpenPopup(kPinMenu);
    }

    if (!ImGui::BeginPopup(kPinMenu))
        return;

    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        editor.deselectPopupPin();

    if (editor.selectedLink != RenderGraph::kInvalidIndex)
    {
        const auto& link = editor.getLink(static_cast<int32_t>(editor.selectedLink));
        const auto& [src, source] = *static_cast<decltype(mRenderPassUI)::value_type*>(link.InputNode->user_ptr);
        const auto& [dst, target] = *static_cast<decltype(mRenderPassUI)::value_type*>(link.OutputNode->user_ptr);
        const auto& output = source.mOutputPins[link.InputSlot].mPinName;
        const auto& input = target.mInputPins[link.OutputSlot].mPinName;
        if (output.starts_with('#'))
            ImGui::Text("%s → %s", src.c_str(), dst.c_str());
        else
            ImGui::Text("%s.%s → %s.%s", src.c_str(), output.c_str(), dst.c_str(), input.c_str());
    }
    else if (const auto index = editor.getPopupPinIndex(); index != RenderGraph::kInvalidIndex)
    {
        auto& [name, passUI] = *static_cast<decltype(mRenderPassUI)::value_type*>(editor.getPopupNode()->user_ptr);
        const bool isInput = editor.isPopupPinInput();
        const auto& pin = (isInput ? passUI.mInputPins : passUI.mOutputPins)[index];
        if (pin.mPinName.starts_with('#'))
            ImGui::TextUnformatted("Execution dependency");
        else
            passUI.renderPinUI(name, this, index, isInput);
    }
    else
    {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void RenderGraphUI::renderUI(RenderContext* pRenderContext, [[maybe_unused]] Gui* pGui)
{
    DCHECK(pGui && pRenderContext && mpRenderGraph);
    if (!mpNodeGraphEditor)
        mpNodeGraphEditor = std::make_shared<NodeGraphEditorGui>(this);
    if (mRebuildDisplayData)
        updateDisplayData(pRenderContext);

    const auto addPass = [&](const std::string& type)
    {
        auto name = type;
        uint32_t suffix = 1;
        while (mpRenderGraph->doesPassExist(name))
            name = type + std::to_string(suffix++);
        addRenderPass(name, type);
    };
    ImGui::PushID(this);
    if (ImGui::BeginCombo("Add pass", nullptr, ImGuiComboFlags_NoPreview))
    {
        auto infos = PluginManager::instance().getInfos<RenderPass>();
        std::ranges::sort(infos, {}, &decltype(infos)::value_type::first);
        for (const auto& [type, info] : infos)
        {
            if (ImGui::Selectable(type.c_str()))
                addPass(type);
            ImGui::SetItemTooltip("%s", info.desc.c_str());
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        mpNodeGraphEditor->selectedLink = RenderGraph::kInvalidIndex;
    mpNodeGraphEditor->render();
    renderPopupMenu();
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kRenderPassType))
        {
            const auto* begin = static_cast<const char*>(payload->Data);
            const std::string_view text(begin, static_cast<size_t>(payload->DataSize));
            addPass(std::string(text.substr(0, text.find('\0'))));
        }
        ImGui::EndDragDropTarget();
    }

    ImVector<ImGui::Node*> selected;
    mpNodeGraphEditor->getSelectedNodes(selected);
    if (!selected.empty())
    {
        constexpr ImVec2 kInspectorSize{320.0f, 380.0f};
        ImGui::SetNextWindowSize(kInspectorSize, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Render UI"))
        {
            Gui::Widgets widgets;
            for (const auto* node : selected)
            {
                auto& [name, passUI] = *static_cast<decltype(mRenderPassUI)::value_type*>(node->user_ptr);
                const auto& pass = mpRenderGraph->getPass(name);
                if (auto group = widgets.group(name.c_str(), true))
                {
                    ImGui::TextWrapped("%s", pass->getDesc().c_str());
                    if (pRenderContext->passEditor)
                    {
                        auto passEditor = *pRenderContext->passEditor;
                        passEditor.submit = [this](const RenderPass& p, std::optional<nlohmann::json> values)
                        {
                            const auto properties = values
                                ? Properties{Properties::json(*values)}
                                : RenderPass::create(p.getType(), mpRenderGraph->getDevice())->getProperties();
                            mpIr->updatePass(p.getName(), properties);
                            mShouldUpdate = true;
                        };
                        auto context = *pRenderContext;
                        context.passEditor = &passEditor;
                        pass->renderUI(&context, widgets);
                    }

                    for (const bool input : {true, false})
                    {
                        if (auto pinGroup = widgets.group(input ? "Inputs" : "Outputs", true))
                        {
                            const auto& pins = input ? passUI.mInputPins : passUI.mOutputPins;
                            for (const auto& pin : pins)
                            {
                                if (pin.mPinName.starts_with('#'))
                                    continue;
                                widgets.pushId(pin.mPinName.c_str());
                                passUI.renderPinUI(name, this, pin.mGuiPinID, input);
                                widgets.popId();
                            }
                        }
                    }
                }
            }
        }
        ImGui::End();
    }
    ImGui::PopID();
}
}  // namespace Falcor
