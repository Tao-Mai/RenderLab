#pragma once

#include "core/Debug.h"

#include "render/FrameData.h"
#include "render/RenderConfig.h"
#include "render/PreparedScene.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/resources/ShaderManager.h"
#include "render/device/Device.h"
#include "render/device/FrameContext.h"
#include "render/device/VulkanContext.h"
#include "render/device/VulkanResources.h"
#include "render/graph/RenderGraph.h"
#include "render/editor/EditorFrameInput.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/device/Swapchain.h"
#include "render/resources/MaterialTable.h"
#include "render/raytracing/offline/OfflineScene.h"
#include "render/raytracing/offline/OfflineTracePipelineCache.h"
#include "render/resources/RenderResourceManager.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

struct SceneRenderData;
struct PassEditorContext;
class Profiler;

class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void              init();
    void              recreateSwapchain();
    RenderFrameResult render(
        const RenderView& view, const SceneRenderData& scene, const EditorFrameInput& editor, float deltaSeconds
    );
    void              waitIdle();
    void              shutdown() noexcept;

    const PreparedScene& prepare(const RenderView& view, const SceneRenderData& scene, uint32_t targetFrameIndex);

    [[nodiscard]] VulkanContext& vulkanContext() { return vulkan; }
    [[nodiscard]] Swapchain&     swapchainHandle() { return swapchain; }
    [[nodiscard]] Falcor::RenderGraph* getActiveGraph() const { return mpRenderGraph.get(); }
    [[nodiscard]] bool supportsObjectPicking() const { return static_cast<bool>(pickingOutput); }
    [[nodiscard]] ExposureConfig defaultExposure() const;
    void renderUI(const PassEditorContext& context);
    void addOption(std::string_view name, nlohmann::json value);
    void addOptions(const nlohmann::json& options);
    void clearOptions();
    [[nodiscard]] uint32_t       framesInFlight() const { return activeFramesInFlight; }

private:
    DEBUG_ONLY(bool inited = false;)
    VulkanContext                               vulkan;
    std::unique_ptr<VulkanResources>             commandResources;
    Swapchain                                   swapchain;
    ShaderManager                               shaders;
    Falcor::ref<Falcor::Device>                                 device = Falcor::make_ref<Falcor::Device>();
    RenderResourceManager                       resources;
    SceneAccelerationStructures                 sceneAccelerationStructures;
    MaterialTable                               materialTable;
    OfflineScene                                offlineScene;
    OfflineTracePipelineCache                   offlinePipelines;
    std::array<FrameContext, maxFramesInFlight> frames;
    std::array<FrameData, maxFramesInFlight>    frameStorage;
    std::array<PreparedScene, maxFramesInFlight>   renderStorage;
    std::optional<EnvironmentMapAsset::ID>      environmentMap;
    std::shared_ptr<GpuTexture>                 radianceTexture;
    std::shared_ptr<GpuTexture>                 irradianceTexture;
    std::shared_ptr<GpuTexture>                 prefilteredSpecularTexture;
    std::shared_ptr<GpuTexture>                 brdfLutTexture;
    std::shared_ptr<GpuTexture>                 sheenLutTexture;
    float                                       prefilteredMaxLod = 0.0f;
    const RendererConfig*                       rendererConfig    = nullptr;
    nlohmann::json appliedParameters;
    std::optional<nlohmann::json> pendingOptions;
    Falcor::Properties::json parameterDefaults = Falcor::Properties::json::object();
    Falcor::Properties::json parameters = Falcor::Properties::json::object();
    std::map<std::pair<std::string, std::string>, nlohmann::json> scriptedPassProperties;
    uint32_t                     frameIndex           = 0;
    uint32_t                     activeFramesInFlight = maxFramesInFlight;
    Falcor::ref<Falcor::RenderGraph> mpRenderGraph;
    Falcor::ref<Falcor::RenderPass>                               exposurePass;
    Falcor::ref<Falcor::Buffer>                                   lightResource;
    Falcor::ref<Falcor::Texture>                                  pickingOutput;
    Falcor::ref<Falcor::Buffer>                                   pickingReadback;
    VulkanResources::BufferHandle                 lightBuffer;
    DescriptorSetLayout                           blitSetLayout;
    std::vector<vk::raii::DescriptorSet>           blitSets;
    vk::PipelineLayout                            blitPipelineLayout;
    vk::Pipeline                                  blitPipeline;
    uint32_t                                      blitSourceBinding = 0;

    void                      applyConfiguration();
    void                      onOptionsChange();
    void                      applyPassProperties(Falcor::RenderGraph& renderGraph, const nlohmann::json& properties);
    const nlohmann::json&     getScriptedPassProperties(const Falcor::RenderPass& pass);
    void                      loadScript(const std::filesystem::path& path);
    void                      setActiveGraph(const Falcor::ref<Falcor::RenderGraph>& renderGraph);
    void                      initFrameResources();
    void                      initBlit();
    void                      endFrame(Falcor::RenderContext& context, VulkanResources::ImageHandle backBuffer);
    void                      initLightingResources();
    void                      updateEnvironmentBindings(const std::optional<EnvironmentMapAsset::ID>& id);
    void                      bindEnvironmentTextures();

    [[nodiscard]] FrameContext& currentFrame() { return frames[frameIndex]; }
};
