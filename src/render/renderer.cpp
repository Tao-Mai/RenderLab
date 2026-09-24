#include "render/renderer.h"

#include "asset/asset_desc.h"
#include "asset/asset_manager.h"
#include "camera.h"
#include "core/context.h"
#include "core/logger.h"
#include "editor/editor.h"
#include "ecs/light.h"
#include "ecs/transform.h"
#include "render/device/vk_check.h"
#include "core/window.h"

#include <iostream>

#include <glm/glm.hpp>

Renderer::~Renderer()
{
    shutdown();
}

void Renderer::init()
{
    if (inited)
    {
        return;
    }
    initVulkan();
    inited = true;
}

EditorFrameResult Renderer::render(const Camera& camera, const EditorFrameInput& editor)
{
    CHECK(inited, "Renderer must be initialized before render()");

    const glm::mat4 view       = camera.viewMatrix();
    const glm::mat4 projection = camera.projectionMatrix(editor.aspectRatio);
    scenePass.updateScene(projection * view, camera.worldPosition(), scene.primaryLightObject());
    return drawFrame(editor);
}

void Renderer::loadScene(SceneDesc& targetScene)
{
    CHECK(inited, "Renderer must be initialized before loading a scene");

    waitIdle();
    scene.load(targetScene, resources);
    scenePass.bindEnvironment(targetScene);
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
    pickingPass.reset();
    resources.reset();
    scenePass.reset();
    frame.reset();
    swapchain.reset();
    vulkan.reset();
    inited = false;
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

void Renderer::initVulkan()
{
    CHECK(context().window != nullptr, "Window must exist before Renderer");
    CHECK(context().assetManager != nullptr, "AssetManager must exist before Renderer");

    Window& window = *context().window;
    vulkan.init(window);
    swapchain.init(vulkan, window);
    frame.init(vulkan);
    swapchain.transitionDepthImageLayout(frame.commandPoolHandle());
    resources.init(vulkan, frame);
    scenePass.init(vulkan, swapchain, frame, resources);
    pickingPass.init(
        vulkan.physicalDeviceHandle(),
        vulkan.deviceHandle(),
        swapchain.extent(),
        swapchain.depthImageFormat(),
        scenePass.sceneLayoutHandle(),
        resources.shader("picking"));
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
    scene.reset();
    pickingPass.reset();
    resources.reset();
    scenePass.reset();
    frame.reset();
    swapchain.reset();

    swapchain.init(vulkan, window);
    frame.init(vulkan);
    swapchain.transitionDepthImageLayout(frame.commandPoolHandle());
    resources.init(vulkan, frame);
    scenePass.init(vulkan, swapchain, frame, resources);
    pickingPass.init(
        vulkan.physicalDeviceHandle(),
        vulkan.deviceHandle(),
        swapchain.extent(),
        swapchain.depthImageFormat(),
        scenePass.sceneLayoutHandle(),
        resources.shader("picking"));
    scene.load(*context().scene, resources);
    scenePass.bindEnvironment(*context().scene);
    context().editor->refreshUi();
}

void Renderer::recordCommandBuffer(uint32_t imageIndex, const EditorFrameInput& editor)
{
    auto& commandBuffer = frame.commandBufferHandle();
    vkCheck(commandBuffer.reset());
    vkCheck(commandBuffer.begin({}));

    transitionImageLayout(
        imageIndex,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    scenePass.record(
        commandBuffer,
        scene.renderItems(),
        scene.lightRenderItems(),
        scene.lightMarkers(),
        {.imageIndex = imageIndex,
         .viewport = editor.viewport,
         .storeDepthForPicking = editor.requestPick});
    recordPickingPass(commandBuffer, editor);
    recordUiPass(commandBuffer, imageIndex, editor);

    transitionImageLayout(
        imageIndex,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe);
    vkCheck(commandBuffer.end());
}

void Renderer::recordPickingPass(
    vk::raii::CommandBuffer& commandBuffer,
    const EditorFrameInput&  editor)
{
    if (!editor.requestPick)
    {
        return;
    }

    pickingPass.begin(
        commandBuffer,
        swapchain.depthImageHandle(),
        swapchain.depthImageViewHandle(),
        scenePass.sceneSetHandle(),
        editor.pickX,
        editor.pickY);

    for (const SceneRenderItem& item : scene.renderItems())
    {
        pickingPass.draw(
            commandBuffer,
            *item.mesh,
            item.object->components.at("Transform").try_cast<ecs::Transform>()->matrix(),
            item.selectionId);
    }

    for (const LightRenderItem& item : scene.lightRenderItems())
    {
        const auto* light = item.object->components.at("Light").try_cast<ecs::Light>();
        const auto* transform =
            item.object->components.at("Transform").try_cast<ecs::Transform>();
        scene.lightMarkers().recordPicking(
            commandBuffer,
            pickingPass.layout(),
            *transform,
            *light,
            item.selectionId);
    }
    pickingPass.end(commandBuffer, editor.pickX, editor.pickY);
}

void Renderer::recordUiPass(
    vk::raii::CommandBuffer& commandBuffer,
    uint32_t                 imageIndex,
    const EditorFrameInput&  editor)
{
    if (!editor.recordUi)
    {
        return;
    }

    const vk::RenderingAttachmentInfo colorAttachment{
        .imageView = swapchain.imageView(imageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    const vk::RenderingInfo renderingInfo{
        .renderArea = {.offset = {0, 0}, .extent = swapchain.extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };
    commandBuffer.beginRendering(renderingInfo);
    editor.recordUi(*commandBuffer);
    commandBuffer.endRendering();
}

void Renderer::transitionImageLayout(
    uint32_t                imageIndex,
    vk::ImageLayout         oldLayout,
    vk::ImageLayout         newLayout,
    vk::AccessFlags2        srcAccessMask,
    vk::AccessFlags2        dstAccessMask,
    vk::PipelineStageFlags2 srcStageMask,
    vk::PipelineStageFlags2 dstStageMask)
{
    auto&                     commandBuffer = frame.commandBufferHandle();
    vk::ImageMemoryBarrier2   barrier       = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain.image(imageIndex),
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1}};
    vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier};
    commandBuffer.pipelineBarrier2(dependencyInfo);
}

EditorFrameResult Renderer::drawFrame(const EditorFrameInput& editor)
{
    const vk::Fence     drawFence                = frame.drawFenceHandle();
    const vk::Semaphore presentCompleteSemaphore = frame.imageAvailableSemaphore();
    const vk::Semaphore renderFinishedSemaphore  = frame.renderFinishedSemaphore();
    const vk::CommandBuffer commandBuffer        = *frame.commandBufferHandle();

    const auto fenceResult = vulkan.deviceHandle().waitForFences(drawFence, vk::True, UINT64_MAX);
    CHECK(fenceResult == vk::Result::eSuccess, "failed to wait for fence!");

    uint32_t imageIndex = 0;
    const vk::Result acquireResult = static_cast<vk::Result>(
        swapchain.handle().getDispatcher()->vkAcquireNextImageKHR(
            static_cast<VkDevice>(*vulkan.deviceHandle()),
            static_cast<VkSwapchainKHR>(*swapchain.handle()),
            UINT64_MAX,
            static_cast<VkSemaphore>(presentCompleteSemaphore),
            VK_NULL_HANDLE,
            &imageIndex));
    if (acquireResult == vk::Result::eErrorOutOfDateKHR)
    {
        recreateSwapchain();
        return {};
    }
    CHECK(acquireResult == vk::Result::eSuccess ||
            acquireResult == vk::Result::eSuboptimalKHR,
        "vkAcquireNextImageKHR failed: {}", vk::to_string(acquireResult));
    vkCheck(vulkan.deviceHandle().resetFences(drawFence));

    const bool resolvePickAfterSubmit = editor.requestPick;
    recordCommandBuffer(imageIndex, editor);

    vkCheck(vulkan.queueHandle().waitIdle());

    vk::PipelineStageFlags waitDestinationStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentCompleteSemaphore,
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
    const vk::Result presentResult = static_cast<vk::Result>(
        vulkan.queueHandle().getDispatcher()->vkQueuePresentKHR(
            static_cast<VkQueue>(*vulkan.queueHandle()),
            reinterpret_cast<const VkPresentInfoKHR*>(&presentInfoKHR)));
    CHECK(presentResult == vk::Result::eSuccess ||
            presentResult == vk::Result::eSuboptimalKHR ||
            presentResult == vk::Result::eErrorOutOfDateKHR,
        "vkQueuePresentKHR failed: {}", vk::to_string(presentResult));

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
        frameResult.pickedSelectionId = pickingPass.readSelectionId();
    }
    if (acquireResult == vk::Result::eSuboptimalKHR ||
        presentResult == vk::Result::eSuboptimalKHR ||
        presentResult == vk::Result::eErrorOutOfDateKHR)
    {
        recreateSwapchain();
    }
    return frameResult;
}
