#include "render/pass/RenderGraph.h"

#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "scene/SceneManager.h"
#include "render/Renderer.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/pass/EditorPickingPass.h"
#include "render/pass/EditorPickingCopyPass.h"
#include "render/pass/EditorPickingReadbackPass.h"
#include "render/pass/EditorUiPass.h"
#include "render/pass/ScenePass.h"
#include "render/pass/ShadowPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/raytracing/RayTracingBlitPass.h"
#include "render/resource/GpuTexture.h"

#include <array>
#include <format>
#include <map>

struct RenderGraph::Passes
{
    ShadowPass shadow;
    ScenePass scene;
    RayTracingPass raytracing;
    RayTracingBlitPass raytracingBlit;
    EditorPickingPass picking;
    EditorPickingCopyPass pickingCopy;
    EditorPickingReadbackPass pickingReadback;
    EditorUiPass ui;
};

struct RenderGraph::Allocation
{
    struct Instance
    {
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::Image ownedImage = nullptr;
        vk::raii::ImageView ownedView = nullptr;
        std::map<std::pair<uint32_t, uint32_t>, vk::raii::ImageView> layerViews;
        std::unique_ptr<Buffer> ownedBuffer;
        ExternalResource external;
        ResourceState state;

        [[nodiscard]] vk::Image image() const { return external.image ? external.image : *ownedImage; }
        [[nodiscard]] vk::ImageView view() const { return external.view ? external.view : *ownedView; }
        [[nodiscard]] Buffer& buffer() const { return external.buffer ? *external.buffer : *ownedBuffer; }
    };
    std::vector<Instance> instances;
};

void RenderGraph::init()
{
    DCHECK(!renderer, "RenderGraph is already initialized");
    renderer = context().renderer;
    DCHECK(renderer);

    const auto chooseDepthFormat = [&](vk::FormatFeatureFlags features)
    {
        for (vk::Format format : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm})
        {
            if ((vulkan().physicalDeviceHandle().getFormatProperties(format).optimalTilingFeatures &
                    features) == features)
            {
                return format;
            }
        }
        CHECK(false, "no depth format supports the render graph's required features");
        return vk::Format::eUndefined;
    };
    sceneDepthFormat = chooseDepthFormat(vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    pointShadowFormat = chooseDepthFormat(vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage);

    const TextureBinding& brdf = context().config->rendererConfig().brdfLut;
    CHECK(!brdf.textureID.empty() && !brdf.samplerID.empty(), "Renderer requires a BRDF LUT binding");
    const TextureAsset& desc = context().assetManager->get<TextureAsset>(brdf.textureID);
    CHECK(desc.layout == ImageLayout::Image2D && desc.colorSpace == ColorSpace::Linear &&
        desc.format != ImageFormat::R8,
        "BRDF LUT must be a linear 2D texture with at least two channels");

    passes = std::make_shared<Passes>();
    passes->scene.init(*this,
        assetResources().texture(brdf.textureID), assetResources().sampler(brdf.samplerID));
    passes->shadow.init(*this);
    passes->picking.init(*this);

    passes->shadow.registerPass(*this);
    passes->scene.registerPass(*this);
    passes->raytracing.registerPass(*this);
    setPassEnabled("raytracing", false);
    passes->raytracingBlit.registerPass(*this);
    setPassEnabled("raytracing_blit", false);
    passes->picking.registerPass(*this);
    passes->pickingCopy.registerPass(*this);
    passes->pickingReadback.registerPass(*this);
    passes->ui.registerPass(*this);
}

void RenderGraph::reset() noexcept
{
    resources.clear();
    orderedPasses.clear();
    registeredPasses.clear();
    passInfos.clear();
    globalInputSlots.clear();
    globalOutputSlots.clear();
    passes.reset();
    renderer = nullptr;
    currentEditor = nullptr;
    sceneDepthFormat = vk::Format::eUndefined;
    pointShadowFormat = vk::Format::eUndefined;
}

void RenderGraph::build()
{
    DCHECK(renderer && passes, "RenderGraph must be initialized before build");
    renderer->waitIdle();
    const bool rayTracing = context().config->rendererConfig().renderPath == RenderPath::RayTracing;
    if (rayTracing != passEnabled("raytracing"))
    {
        setPassEnabled("raytracing", rayTracing);
        setPassEnabled("raytracing_blit", rayTracing);
        setPassEnabled("scene", !rayTracing);
        setPassEnabled("shadow", !rayTracing);
    }
    resources.clear();
    for (Slot& slot : globalInputSlots) slot = {.owner = slot.owner};
    for (Slot& slot : globalOutputSlots) slot = {.owner = slot.owner};

    for (PassInfo* pass : registeredPasses)
    {
        if (pass->enabled)
        {
            DCHECK(pass->setupPass && pass->executePass, "pass '{}' has no callbacks", pass->name);
            pass->setupPass(*this);
        }
    }
    validateCompilation();

    allocateResources();
    if (passEnabled("shadow")) passes->shadow.bindResources(*this);
}

void RenderGraph::validateCompilation()
{
    const auto errors = compile();
    std::string message;
    for (const auto& error : errors) message += "\n  " + error;
    DCHECK(errors.empty(), "RenderGraph compilation failed:{}", message);
}

void RenderGraph::bindEnvironmentTextures()
{
    DCHECK(passes);
    passes->scene.bindEnvironmentTextures();
}

void RenderGraph::allocateResources()
{
    const auto& device = vulkan().deviceHandle();
    const auto& physicalDevice = vulkan().physicalDeviceHandle();
    for (Resource& resource : resources)
    {
        resource.allocation = std::make_shared<Allocation>();
        const ResourceDesc& desc = resource.merged;
        const size_t count = resource.external ? resource.instances.size() : maxFramesInFlight;
        resource.allocation->instances.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto& instance = resource.allocation->instances[i];
            if (resource.external)
            {
                instance.external = resource.instances[i];
                instance.state = resource.instances[i].state;
                continue;
            }
            if (desc.type == ResourceType::Buffer)
            {
                instance.ownedBuffer = std::make_unique<Buffer>(physicalDevice, device,
                    desc.size, desc.bufferUsage, desc.memoryProperties);
                continue;
            }

            instance.ownedImage = vkCheck(device.createImage({
                .flags = desc.imageFlags,
                .imageType = vk::ImageType::e2D,
                .format = desc.format,
                .extent = desc.extent,
                .mipLevels = desc.mipLevels,
                .arrayLayers = desc.arrayLayers,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = vk::ImageTiling::eOptimal,
                .usage = desc.imageUsage,
                .sharingMode = vk::SharingMode::eExclusive,
                .initialLayout = vk::ImageLayout::eUndefined,
            }));
            const auto requirements = instance.ownedImage.getMemoryRequirements();
            instance.memory = vkCheck(device.allocateMemory({
                .allocationSize = requirements.size,
                .memoryTypeIndex = vulkan_memory::findType(physicalDevice,
                    requirements.memoryTypeBits, desc.memoryProperties),
            }));
            vkCheck(instance.ownedImage.bindMemory(*instance.memory, 0));
            instance.ownedView = vkCheck(device.createImageView({
                .image = *instance.ownedImage,
                .viewType = desc.viewType,
                .format = desc.format,
                .subresourceRange = {.aspectMask = desc.aspect,
                    .levelCount = desc.mipLevels, .layerCount = desc.arrayLayers},
            }));
        }
    }
}

void RenderGraph::importSwapchain(ResourceHandle input, const ResourceDesc& use)
{
    ResourceDesc desc = use;
    desc.format = swapchain().surfaceFormat().format;
    const auto extent = swapchain().extent();
    desc.extent = vk::Extent3D{extent.width, extent.height, 1};
    desc.imageUsage |= vk::ImageUsageFlagBits::eColorAttachment;
    std::vector<ExternalResource> instances;
    for (uint32_t i = 0; i < swapchain().imageCount(); ++i)
        instances.push_back({.image = swapchain().image(i), .view = swapchain().imageView(i)});
    importResource(input, desc, std::move(instances), true);
}

void RenderGraph::importTexture(ResourceHandle input, const GpuTexture& texture)
{
    const auto& info = texture.imageInfo();
    const ResourceDesc desc{
        .format = info.format,
        .extent = info.extent,
        .mipLevels = info.mipLevels,
        .arrayLayers = info.arrayLayers,
        .imageFlags = info.flags,
        .viewType = (info.flags & vk::ImageCreateFlagBits::eCubeCompatible)
            ? vk::ImageViewType::eCube : vk::ImageViewType::e2D,
        .imageUsage = info.usage,
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    importResource(input, desc,
        {{.image = texture.imageHandle(), .view = texture.imageView(),
            .state = {.stages = desc.stages, .access = desc.access, .layout = desc.layout}}});
}

uint32_t RenderGraph::instanceIndex(const Resource& resource, uint32_t frameIndex) const
{
    if (resource.swapchainIndexed) return currentImage;
    return resource.allocation->instances.size() == 1 ? 0 : frameIndex;
}

RenderGraph::Allocation& RenderGraph::allocation(ResourceHandle input) const
{
    DCHECK(globalInputSlots.at(input).resource != invalidHandle);
    auto& resource = resources.at(globalInputSlots.at(input).resource);
    DCHECK(resource.allocation);
    return *resource.allocation;
}

vk::Image RenderGraph::image(ResourceHandle input) const
{
    const auto& resource = resources.at(globalInputSlots.at(input).resource);
    return allocation(input).instances.at(instanceIndex(resource, currentFrame)).image();
}

vk::ImageView RenderGraph::imageView(ResourceHandle input) const
{
    return imageView(input, currentFrame);
}

vk::ImageView RenderGraph::imageView(ResourceHandle input, uint32_t frameIndex) const
{
    const auto& resource = resources.at(globalInputSlots.at(input).resource);
    return allocation(input).instances.at(instanceIndex(resource, frameIndex)).view();
}

vk::ImageView RenderGraph::layerView(ResourceHandle input, uint32_t firstLayer, uint32_t layerCount) const
{
    const auto& resource = resources.at(globalInputSlots.at(input).resource);
    const auto& desc = resource.merged;
    DCHECK(desc.type == ResourceType::Image && layerCount &&
        firstLayer < desc.arrayLayers && layerCount <= desc.arrayLayers - firstLayer);

    auto& instance = allocation(input).instances.at(instanceIndex(resource, currentFrame));
    const std::pair key{firstLayer, layerCount};
    if (const auto found = instance.layerViews.find(key); found != instance.layerViews.end())
        return *found->second;

    auto view = vkCheck(vulkan().deviceHandle().createImageView({
        .image = instance.image(),
        .viewType = layerCount == 1 ? vk::ImageViewType::e2D : vk::ImageViewType::e2DArray,
        .format = desc.format,
        .subresourceRange = {.aspectMask = desc.aspect, .levelCount = 1,
            .baseArrayLayer = firstLayer, .layerCount = layerCount},
    }));
    const vk::ImageView handle = *view;
    instance.layerViews.emplace(key, std::move(view));
    return handle;
}

Buffer& RenderGraph::buffer(ResourceHandle input) const { return buffer(input, currentFrame); }

Buffer& RenderGraph::buffer(ResourceHandle input, uint32_t frameIndex) const
{
    const auto& resource = resources.at(globalInputSlots.at(input).resource);
    return allocation(input).instances.at(instanceIndex(resource, frameIndex)).buffer();
}

void RenderGraph::useResource(ResourceHandle index, const ResourceDesc& use)
{
    Resource& resource = resources.at(index);
    auto& instance = resource.allocation->instances.at(instanceIndex(resource, currentFrame));
    const ResourceState previous = instance.state;
    if (!needsBarrier(previous, use))
    {
        instance.state.stages |= use.stages;
        instance.state.access |= use.access;
        return;
    }

    if (resource.merged.type == ResourceType::Image)
    {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = previous.stages,
            .srcAccessMask = previous.access,
            .dstStageMask = use.stages,
            .dstAccessMask = use.access,
            .oldLayout = previous.layout,
            .newLayout = use.layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = instance.image(),
            .subresourceRange = {.aspectMask = resource.merged.aspect,
                .levelCount = resource.merged.mipLevels, .layerCount = resource.merged.arrayLayers},
        };
        commands().pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }
    else
    {
        const vk::BufferMemoryBarrier2 barrier{
            .srcStageMask = previous.stages,
            .srcAccessMask = previous.access,
            .dstStageMask = use.stages,
            .dstAccessMask = use.access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = instance.buffer().handle(),
            .size = resource.merged.size,
        };
        commands().pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
    }
    instance.state = {.stages = use.stages, .access = use.access, .layout = use.layout};
}

void RenderGraph::execute(const EditorFrameInput& editor,
    uint32_t frameIndex, uint32_t imageIndex)
{
    currentFrame = frameIndex;
    currentImage = imageIndex;
    currentEditor = &editor;
    for (Resource& resource : resources)
    {
        if (resource.swapchainIndexed)
        {
            // Match Renderer::drawFrame's acquire semaphore wait stage so the
            // first layout transition is part of that execution dependency.
            auto& state = resource.allocation->instances.at(currentImage).state;
            state.stages = passEnabled("raytracing") ? vk::PipelineStageFlagBits2::eTransfer
                : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            state.access = {};
        }
    }

    vkCheck(commands().reset());
    vkCheck(commands().begin({}));
    for (PassInfo* pass : orderedPasses)
    {
        for (ResourceHandle input : pass->inputResourceHandles)
        {
            const Slot& slot = globalInputSlots[input];
            if (slot.binding != Binding::Ignored) useResource(slot.resource, slot.desc);
        }
        pass->executePass(*this);
    }
    for (ResourceHandle index = 0; index < resources.size(); ++index)
    {
        if (resources[index].swapchainIndexed)
            useResource(index, {.layout = vk::ImageLayout::ePresentSrcKHR});
    }
    vkCheck(commands().end());
    currentEditor = nullptr;
}

uint32_t RenderGraph::readSelectionId(uint32_t frameIndex)
{
    DCHECK(passEnabled("editor_picking_readback"));
    return passes->pickingReadback.readSelectionId(*this, frameIndex);
}

VulkanContext& RenderGraph::vulkan() const { return renderer->vulkan; }
Swapchain& RenderGraph::swapchain() const { return renderer->swapchain; }
PipelineManager& RenderGraph::pipelines() const { return renderer->pipelines; }
ShaderManager& RenderGraph::shaders() const { return renderer->shaders; }
RenderResourceManager& RenderGraph::assetResources() const { return renderer->resources; }
FrameData& RenderGraph::frameData(uint32_t index) const { return renderer->frames.at(index).data; }
const RenderData& RenderGraph::renderData() const { return renderer->renderSystem.renderData(); }
const LightMarkers& RenderGraph::lightMarkers() const { return renderer->renderSystem.lightMarkers(); }
vk::Format RenderGraph::depthFormat() const { return sceneDepthFormat; }
vk::Format RenderGraph::shadowFormat() const { return pointShadowFormat; }
vk::raii::CommandBuffer& RenderGraph::commands() const { return renderer->frames.at(currentFrame).commandBufferHandle(); }
uint32_t RenderGraph::frameIndex() const { return currentFrame; }
const EditorFrameInput& RenderGraph::editorInput() const
{
    DCHECK(currentEditor);
    return *currentEditor;
}
