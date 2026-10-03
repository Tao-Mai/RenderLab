#include "render/Renderer.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "scene/component/LightComponent.h"
#include "scene/component/TransformComponent.h"
#include "scene/SceneManager.h"

#include "scene/component/CameraComponent.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "editor/Editor.h"
#include "render/device/VkCheck.h"
#include "core/Window.h"

#include <limits>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>

Renderer::~Renderer()
{
    DCHECK(!inited);
}

void Renderer::init()
{
    DCHECK(!inited);

    initVulkan();
    DEBUG_EXEC(inited = true);
}

EditorFrameResult Renderer::render(const CameraComponent& camera, const EditorFrameInput& editor)
{
    DCHECK(inited);
    return drawFrame(camera, editor);
}

void Renderer::loadScene()
{
    DCHECK(inited);

    waitIdle();
    DCHECK(context().sceneManager);
    const auto& targetScene = context().sceneManager->scene();
    scene.load(resources);
    graph.bindSceneTextures(targetScene);
    iblParameters = {};
    if (targetScene.environment.environmentMap)
    {
        const auto& environment = context().assetManager->get<EnvironmentMapAsset>(
            *targetScene.environment.environmentMap);
        const auto& prefiltered = context().assetManager->get<TextureAsset>(
            environment.prefilteredSpecular.textureID);
        iblParameters.x = static_cast<float>(prefiltered.mipLevels - 1);
    }
    graph.build();
}

void Renderer::waitIdle()
{
    if (*vulkan.deviceHandle())
    {
        vkCheck(vulkan.deviceHandle().waitIdle());
    }
}

void Renderer::shutdown() noexcept
{
    if (*vulkan.deviceHandle())
    {
        (void)vulkan.deviceHandle().waitIdle();
    }

    scene.reset();
    graph.reset();
    resources.reset();
    pipelines.reset();
    for (FrameContext& frame : frames)
    {
        frame.reset();
    }
    descriptors.reset();
    shaders.reset();
    swapchain.reset();
    vulkan.reset();
    frameIndex = 0;
    iblParameters = {};
    DEBUG_EXEC(inited = false);
}

VulkanContext& Renderer::vulkanContext()
{
    return vulkan;
}

Swapchain& Renderer::swapchainHandle()
{
    return swapchain;
}

const GpuScene& Renderer::gpuScene() const
{
    return scene;
}

FrameContext& Renderer::currentFrame()
{
    return frames[frameIndex];
}

void Renderer::initVulkan()
{
    DCHECK(context().window);
    DCHECK(context().assetManager);
    DCHECK(context().config);

    Window& window = *context().window;
    vulkan.init(window);
    swapchain.init(vulkan, window);
    shaders.init(vulkan.deviceHandle());
    descriptors.init(vulkan.deviceHandle());
    pipelines.init(vulkan.deviceHandle(), descriptors, shaders);
    for (FrameContext& frame : frames)
    {
        frame.init(vulkan, descriptors);
    }
    resources.init(vulkan, descriptors, shaders);

    graph.init();
}

void Renderer::recreateSwapchain()
{
    Window& window = *context().window;
    while (!window.shouldClose())
    {
        const auto [width, height] = window.framebufferSize();
        if (width > 0 && height > 0)
        {
            break;
        }
        window.waitEvents();
    }
    if (window.shouldClose())
    {
        return;
    }

    waitIdle();
    swapchain.reset();
    swapchain.init(vulkan, window);
    graph.build();
    context().editor->refreshUi();
}

void Renderer::updateFrameData(const CameraComponent& camera, const EditorFrameInput& editor)
{
    const glm::mat4 viewProjection = camera.projectionMatrix(editor.aspectRatio) * camera.viewMatrix();
    const ViewUniforms view{
        .viewProjection = viewProjection,
        .inverseViewProjection = glm::inverse(viewProjection),
        .cameraPosition = glm::vec4{camera.worldPosition(), 1.0f},
    };

    const auto& lightItems = scene.lightRenderItems();
    CHECK(lightItems.size() <= std::numeric_limits<uint32_t>::max(), "too many scene lights");
    std::vector<LightData> lights;
    lights.reserve(lightItems.size());
    for (const LightRenderItem& item : lightItems)
    {
        const auto* light = item.actor->getComponent<LightComponent>();
        const auto* transform = item.actor->getComponent<TransformComponent>();
        DCHECK(light && transform, "light requires Light and Transform components");

        const glm::vec3 direction = glm::normalize(transform->rotation * glm::vec3{0.0f, 0.0f, -1.0f});
        lights.push_back({
            .colorIntensity = {light->color, light->intensity},
            .positionRange = {transform->position, light->range},
            .direction = {direction, 0.0f},
            .areaSizeCone = {light->areaSize, light->cosInner, light->cosOuter},
            .flags = {static_cast<uint32_t>(light->type), light->enabled ? 1u : 0u,
                light->castShadow ? 1u : 0u, 0u},
        });
    }
    const LightUniforms lighting{
        .lightCount = static_cast<uint32_t>(lights.size()),
        .iblParameters = iblParameters,
    };
    currentFrame().updateFrameData(view, lighting, lights);
}

EditorFrameResult Renderer::drawFrame(const CameraComponent& camera, const EditorFrameInput& editor)
{
    const vk::Fence         drawFence               = currentFrame().drawFenceHandle();
    const vk::Semaphore     imageAvailableSemaphore = currentFrame().imageAvailableSemaphore();
    const vk::CommandBuffer commandBuffer           = *currentFrame().commandBufferHandle();

    const auto fenceResult = vulkan.deviceHandle().waitForFences(drawFence, vk::True, UINT64_MAX);
    CHECK(fenceResult == vk::Result::eSuccess, "failed to wait for fence!");

    uint32_t         imageIndex    = 0;
    const auto acquireResult = static_cast<vk::Result>(
        swapchain.handle().getDispatcher()->vkAcquireNextImageKHR(
            static_cast<VkDevice>(*vulkan.deviceHandle()),
            static_cast<VkSwapchainKHR>(*swapchain.handle()),
            UINT64_MAX,
            static_cast<VkSemaphore>(imageAvailableSemaphore),
            VK_NULL_HANDLE,
            &imageIndex));
    if (acquireResult == vk::Result::eErrorOutOfDateKHR)
    {
        recreateSwapchain();
        return {};
    }
    CHECK(acquireResult == vk::Result::eSuccess ||
          acquireResult == vk::Result::eSuboptimalKHR,
          "vkAcquireNextImageKHR failed: {}",
          vk::to_string(acquireResult));
    const vk::Semaphore renderFinishedSemaphore =
        swapchain.renderFinishedSemaphore(imageIndex);
    vkCheck(vulkan.deviceHandle().resetFences(drawFence));

    const bool resolvePickAfterSubmit = editor.requestPick && graph.passEnabled("editor_picking");
    updateFrameData(camera, editor);
    graph.prepareRenderData(frameIndex);
    graph.execute(editor, frameIndex, imageIndex);

    vk::PipelineStageFlags waitDestinationStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailableSemaphore,
        .pWaitDstStageMask = &waitDestinationStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinishedSemaphore};
    vkCheck(vulkan.queueHandle().submit(submitInfo, drawFence));

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &*swapchain.handle(),
        .pImageIndices = &imageIndex,
    };
    const auto presentResult = static_cast<vk::Result>(
        vulkan.queueHandle().getDispatcher()->vkQueuePresentKHR(
            static_cast<VkQueue>(*vulkan.queueHandle()),
            reinterpret_cast<const VkPresentInfoKHR*>(&presentInfoKHR)));
    CHECK(presentResult == vk::Result::eSuccess ||
          presentResult == vk::Result::eSuboptimalKHR ||
          presentResult == vk::Result::eErrorOutOfDateKHR,
          "vkQueuePresentKHR failed: {}",
          vk::to_string(presentResult));

    EditorFrameResult frameResult;
    if (resolvePickAfterSubmit)
    {
        const vk::Result pickFenceResult = vulkan.deviceHandle().waitForFences(
            drawFence,
            vk::True,
            UINT64_MAX);
        CHECK(pickFenceResult == vk::Result::eSuccess,
              "failed to wait for object picking readback");

        frameResult.hasPickResult     = true;
        frameResult.pickedSelectionId = graph.readSelectionId(frameIndex);
    }
    if (acquireResult == vk::Result::eSuboptimalKHR ||
        presentResult == vk::Result::eSuboptimalKHR ||
        presentResult == vk::Result::eErrorOutOfDateKHR)
    {
        recreateSwapchain();
    }
    frameIndex = (frameIndex + 1) % maxFramesInFlight;
    return frameResult;
}
