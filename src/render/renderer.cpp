#include "render/renderer.h"

#include "asset/asset_manager.h"
#include "camera.h"
#include "asset/asset_desc.h"
#include "window.h"

#include <iostream>

#include <glm/glm.hpp>
#include "logger.h"

Renderer::~Renderer()
{
    shutdown();
}

void Renderer::init(Window& targetWindow, AssetManager& assets)
{
    if (initialized)
    {
        return;
    }
    window = &targetWindow;
    try
    {
        initVulkan(assets);
        initialized = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

EditorFrameResult Renderer::render(const Camera& camera, const EditorFrameInput& editor)
{
    CHECK(initialized, "Renderer must be initialized before render()");

    const glm::mat4 view       = camera.viewMatrix();
    const glm::mat4 projection = camera.projectionMatrix(editor.aspectRatio);
    scenePass.updateScene(projection * view, camera.worldPosition(), scene.primaryLight());
    return drawFrame(editor);
}

void Renderer::loadScene(SceneDesc& targetScene)
{
    CHECK(initialized, "Renderer must be initialized before loading a scene");

    waitIdle();
    scene.load(targetScene, resources);
}

void Renderer::waitIdle()
{
    if (*vulkan.deviceHandle())
    {
        vulkan.deviceHandle().waitIdle();
    }
}

void Renderer::shutdown() noexcept
{
    if (*vulkan.deviceHandle())
    {
        try
        {
            vulkan.deviceHandle().waitIdle();
        }
        catch (...)
        {
        }
    }

    scene.reset();
    pickingPass.reset();
    resources.reset();
    scenePass.reset();
    frame.reset();
    swapchain.reset();
    vulkan.reset();
    window      = nullptr;
    initialized = false;
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

void Renderer::initVulkan(AssetManager& assets)
{
    vulkan.init(*window);
    swapchain.init(vulkan, *window);
    frame.init(vulkan);
    swapchain.transitionDepthImageLayout(frame.commandPoolHandle());
    resources.init(assets, vulkan, frame);
    scenePass.init(vulkan, swapchain, frame, resources);
    pickingPass.init(
        vulkan.physicalDeviceHandle(),
        vulkan.deviceHandle(),
        swapchain.extent(),
        swapchain.depthImageFormat(),
        scenePass.sceneLayoutHandle(),
        resources.shader("shader:picking"));
}

void Renderer::recordCommandBuffer(uint32_t imageIndex, const EditorFrameInput& editor)
{
    auto& commandBuffer = frame.commandBufferHandle();
    commandBuffer.reset();
    commandBuffer.begin({});

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
    commandBuffer.end();
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
            item.object->transform.matrix(),
            item.selectionId);
    }

    for (const LightRenderItem& item : scene.lightRenderItems())
    {
        scene.lightMarkers().recordPicking(
            commandBuffer,
            pickingPass.layout(),
            *item.light,
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

    auto fenceResult = vulkan.deviceHandle().waitForFences(drawFence, vk::True, UINT64_MAX);
    CHECK(fenceResult == vk::Result::eSuccess, "failed to wait for fence!");
    vulkan.deviceHandle().resetFences(drawFence);

    auto [result, imageIndex] = swapchain.handle().acquireNextImage(
        UINT64_MAX,
        presentCompleteSemaphore,
        nullptr);

    const bool resolvePickAfterSubmit = editor.requestPick;
    recordCommandBuffer(imageIndex, editor);

    vulkan.queueHandle().waitIdle();

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
    vulkan.queueHandle().submit(submitInfo, drawFence);

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &*swapchain.handle(),
        .pImageIndices = &imageIndex,
    };
    result = vulkan.queueHandle().presentKHR(presentInfoKHR);
    switch (result)
    {
        case vk::Result::eSuccess:
            break;
        case vk::Result::eSuboptimalKHR:
            std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n";
            break;
        default:
            break;
    }

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
    return frameResult;
}
