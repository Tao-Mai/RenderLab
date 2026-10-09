#pragma once

#include "core/Debug.h"

#include "editor/ViewportRect.h"
#include "render/device/FrameContext.h"
#include "render/RenderConfig.h"
#include "render/device/VulkanContext.h"
#include "render/DescriptorManager.h"
#include "render/PipelineManager.h"
#include "render/ShaderManager.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/editor/LightMarkers.h"
#include "render/present/Swapchain.h"
#include "render/resource/RenderResourceManager.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>

class EditorCamera;
class AActor;
struct RenderData;

struct EditorFrameInput
{
    ViewportRect viewport;
    float        aspectRatio      = 1.0f;
    bool         requestPick      = false;
    uint32_t     pickX            = 0;
    uint32_t     pickY            = 0;
    AActor* selectedActor = nullptr;
    std::function<void(VkCommandBuffer)> recordUi;
};

struct EditorFrameResult
{
    uint32_t pickedSelectionId = 0;
    bool     hasPickResult     = false;
};

class Renderer
{
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init();
    void loadScene();
    EditorFrameResult render(const EditorCamera& camera, const RenderData& renderData, const EditorFrameInput& editor);
    void waitIdle();
    void shutdown() noexcept;

    [[nodiscard]] VulkanContext& vulkanContext();
    [[nodiscard]] Swapchain&     swapchainHandle();

private:
    friend class RenderGraph;
    struct Passes;
    DEBUG_ONLY(bool inited = false;)
    VulkanContext                             vulkan;
    Swapchain                                 swapchain;
    ShaderManager                             shaders;
    DescriptorManager                         descriptors;
    PipelineManager                           pipelines;
    RenderResourceManager                     resources;
    std::array<FrameContext, maxFramesInFlight> frames;
    uint32_t                                  frameIndex = 0;
    LightMarkers                              lightMarkers;
    RenderGraph                               graph;
    std::shared_ptr<Passes>                    passes;
    RenderGraph::BufferHandle                  pickingReadback;
    glm::vec4                                 iblParameters{0.0f};

    void initVulkan();
    void setupRenderGraph();

    template <class T, class... Args>
    auto setupPass(Args&&... args);

    void updateFrameData(const EditorCamera& camera, const RenderData& renderData, const EditorFrameInput& editor);
    [[nodiscard]] FrameContext& currentFrame();
    void recreateSwapchain();
    EditorFrameResult drawFrame(const EditorCamera& camera, const RenderData& renderData, const EditorFrameInput& editor);
};
