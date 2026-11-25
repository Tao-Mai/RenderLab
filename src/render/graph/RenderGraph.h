#pragma once
#include <slang-rhi.h>

#include "core/ds/DirectedGraph.h"
#include "render/graph/RenderGraphCompiler.h"
#include "render/graph/RenderGraphExe.h"

#include <glm/vec2.hpp>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>



class RenderGraph
{
public:
    static constexpr uint32_t        kInvalidIndex = DirectedGraph::kInvalidID;

    RenderGraph(rhi::ComPtr<rhi::IDevice> device, const std::string& name);
    ~RenderGraph();
    uint32_t addPass(const std::shared_ptr<RenderPass>& pass, const std::string& name);
    void     removePass(const std::string& name);
    [[nodiscard]] const std::shared_ptr<RenderPass>& getPass(const std::string& name) const;
    [[nodiscard]] uint32_t               getPassIndex(const std::string& name) const;
    [[nodiscard]] bool doesPassExist(const std::string& name) const { return mNameToIndex.contains(name); }
    void applyPassSettings(const RenderPassOptions& options);

    uint32_t addEdge(const std::string& source, const std::string& target);
    void     removeEdge(const std::string& source, const std::string& target);
    void     removeEdge(uint32_t edgeId);

    void               setInput(const std::string& name, rhi::IResource* resource);
    void               markOutput(const std::string& name);
    void               unmarkOutput(const std::string& name);
    [[nodiscard]] bool isGraphOutput(const std::string& name) const;
    [[nodiscard]] rhi::ComPtr<rhi::IResource>             getOutput(const std::string& name);
    [[nodiscard]] rhi::ComPtr<rhi::IResource>             getOutput(uint32_t index);
    [[nodiscard]] rhi::ComPtr<rhi::ITexture>              getOutputTexture(const std::string& name);
    [[nodiscard]] rhi::ComPtr<rhi::ITexture>              getOutputTexture(uint32_t index);
    [[nodiscard]] rhi::ComPtr<rhi::IBuffer>               getOutputBuffer(const std::string& name);
    [[nodiscard]] size_t                                  getOutputCount() const { return mOutputs.size(); }
    [[nodiscard]] std::string                             getOutputName(size_t index) const;
    [[nodiscard]] std::vector<std::string>                getAvailableOutputs() const;
    [[nodiscard]] bool                                    hasSameTopology(const RenderGraph& other) const;

    [[nodiscard]] const std::string& getName() const { return mName; }
    [[nodiscard]] uint32_t            getMaxFramesInFlight() const { return mpExe->getMaxFramesInFlight(); }
    [[nodiscard]] RenderData::Dictionary& getPassesDictionary() { return mPassesDictionary; }

    void               onResize(glm::uvec2 extent, rhi::Format format);
    void               setScene(GpuScene* scene);
    void               onSceneUpdates(RenderContext* context, SceneUpdateFlags updates);
    [[nodiscard]] bool needsCompilation() const { return !mpExe || mRecompile; }
    void compile(RenderContext* context);
    void execute(RenderContext* context);
    void renderUI(const PassEditorContext& context, Gui::Widgets& widgets, RenderPassOptions& options);

private:
    friend class RenderGraphCompiler;

    struct EdgeData
    {
        std::string srcField;
        std::string dstField;
    };

    struct NodeData
    {
        std::string                 name;
        std::shared_ptr<RenderPass> pPass;
    };

    struct GraphOut
    {
        uint32_t    nodeId = kInvalidIndex;
        std::string field;
        bool operator==(const GraphOut&) const = default;
    };

    [[nodiscard]] uint32_t getEdge(const std::string& source, const std::string& target);
    [[nodiscard]] bool isGraphOutput(const GraphOut& output) const;

    rhi::ComPtr<rhi::IDevice>                 mpDevice;
    std::string                               mName;
    std::unique_ptr<DirectedGraph>            mpGraph;
    std::unordered_map<std::string, uint32_t> mNameToIndex;
    std::unordered_map<uint32_t, EdgeData>    mEdgeData;
    std::unordered_map<uint32_t, NodeData>    mNodeData;
    std::vector<GraphOut>                     mOutputs;
    RenderData::Dictionary                   mPassesDictionary;
    std::unique_ptr<RenderGraphExe>           mpExe;
    RenderGraphCompiler::Dependencies         mCompilerDeps;
    GpuScene*                                 mpScene = nullptr;
    bool                                      mRecompile = false;
};
