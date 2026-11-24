#pragma once
#include <slang-rhi.h>

#include "core/DirectedGraph.h"
#include "core/Macros.h"
#include "core/Object.h"
#include "core/platform/OS.h"
#include "render/ViewportRect.h"
#include "render/graph/RenderGraphCompiler.h"
#include "render/graph/RenderGraphExe.h"
#include "render/graph/RenderGraphFlags.h"

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace Falcor
{


class FALCOR_API RenderGraph : public Object
{
    FALCOR_OBJECT(RenderGraph)

public:
    static const FileDialogFilterVec kFileExtensionFilters;
    static constexpr uint32_t        kInvalidIndex = DirectedGraph::kInvalidID;

    RenderGraph(rhi::ComPtr<rhi::IDevice> device, const std::string& name);
    ~RenderGraph() override;
    [[nodiscard]] static ref<RenderGraph> create(rhi::ComPtr<rhi::IDevice> device, const std::string& name = {});
    [[nodiscard]] static ref<RenderGraph>
    createFromFile(rhi::ComPtr<rhi::IDevice> device, const std::filesystem::path& path);
    [[nodiscard]] static ref<RenderGraph>   createFromString(rhi::ComPtr<rhi::IDevice> device, std::string_view script);
    [[nodiscard]] rhi::ComPtr<rhi::IDevice> getDevice() const { return mpDevice; }
    void                                    setScene(const ref<Scene>& scene);
    [[nodiscard]] const ref<Scene>&         getScene() const { return mpScene; }
    ref<RenderPass> createPass(const std::string& passName, const std::string& passType, const Properties& properties);

    uint32_t                             addPass(const ref<RenderPass>& pass, const std::string& name);
    void                                 removePass(const std::string& name);
    void                                 updatePass(const std::string& name, const Properties& properties);
    [[nodiscard]] const ref<RenderPass>& getPass(const std::string& name) const;
    [[nodiscard]] uint32_t               getPassIndex(const std::string& name) const;
    [[nodiscard]] bool doesPassExist(const std::string& name) const { return mNameToIndex.contains(name); }

    uint32_t addEdge(const std::string& source, const std::string& target);
    void     removeEdge(const std::string& source, const std::string& target);
    void     removeEdge(uint32_t edgeId);

    void               setInput(const std::string& name, rhi::IResource* resource);
    void               markOutput(const std::string& name, TextureChannelFlags mask = TextureChannelFlags::RGB);
    void               unmarkOutput(const std::string& name);
    [[nodiscard]] bool isGraphOutput(const std::string& name) const;
    [[nodiscard]] rhi::ComPtr<rhi::IResource>             getOutput(const std::string& name);
    [[nodiscard]] rhi::ComPtr<rhi::IResource>             getOutput(uint32_t index);
    [[nodiscard]] rhi::ComPtr<rhi::ITexture>              getOutputTexture(const std::string& name);
    [[nodiscard]] rhi::ComPtr<rhi::ITexture>              getOutputTexture(uint32_t index);
    [[nodiscard]] rhi::ComPtr<rhi::IBuffer>               getOutputBuffer(const std::string& name);
    [[nodiscard]] size_t                                  getOutputCount() const { return mOutputs.size(); }
    [[nodiscard]] std::string                             getOutputName(size_t index) const;
    [[nodiscard]] std::unordered_set<TextureChannelFlags> getOutputMasks(size_t index) const;
    [[nodiscard]] std::vector<std::string>                getAvailableOutputs() const;

    [[nodiscard]] const std::string& getName() const { return mName; }
    void                             setName(const std::string& value) { mName = value; }
    [[nodiscard]] Dictionary&        getPassesDictionary() { return mPassesDictionary; }

    void               onResize(Extent2D extent, rhi::Format format);
    [[nodiscard]] bool needsCompilation() const { return !mpExe || mRecompile; }
    bool               compile(RenderContext* context, std::string& log);
    bool               compile(RenderContext* context)
    {
        std::string log;
        return compile(context, log);
    }
    void execute(RenderContext* context);
    void update(const ref<RenderGraph>& graph);
    void renderUI(RenderContext* context, Gui::Widgets& widget);
    void renderOverlayUI(RenderContext* context);
    void onSceneUpdates(RenderContext* context, IScene::UpdateFlags updates);
    bool onMouseEvent(const MouseEvent& event);
    bool onKeyEvent(const KeyboardEvent& event);
    void onHotReload(HotReloadFlags reloaded);

private:
    friend class RenderGraphCompiler;
    friend class RenderGraphExporter;

    struct EdgeData
    {
        std::string srcField;
        std::string dstField;
    };

    struct NodeData
    {
        std::string     name;
        ref<RenderPass> pPass;
    };

    struct GraphOut
    {
        uint32_t                                nodeId = kInvalidIndex;
        std::string                             field;
        std::unordered_set<TextureChannelFlags> masks;
        bool operator==(const GraphOut& other) const { return nodeId == other.nodeId && field == other.field; }
        bool operator!=(const GraphOut& other) const { return !(*this == other); }
    };

    [[nodiscard]] uint32_t getEdge(const std::string& source, const std::string& target);
    RenderPass*            getRenderPassAndNamePair(
                   bool input, const std::string& fullName, std::pair<std::string, std::string>& nameAndField
               ) const;
    [[nodiscard]] bool isGraphOutput(const GraphOut& output) const;

    rhi::ComPtr<rhi::IDevice>                 mpDevice;
    std::string                               mName;
    ref<Scene>                                mpScene;
    std::unique_ptr<DirectedGraph>            mpGraph;
    std::unordered_map<std::string, uint32_t> mNameToIndex;
    std::unordered_map<uint32_t, EdgeData>    mEdgeData;
    std::unordered_map<uint32_t, NodeData>    mNodeData;
    std::vector<GraphOut>                     mOutputs;
    Dictionary                                mPassesDictionary;
    std::unique_ptr<RenderGraphExe>           mpExe;
    RenderGraphCompiler::Dependencies         mCompilerDeps;
    bool                                      mRecompile = false;
};
}  // namespace Falcor
