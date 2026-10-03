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
#include "render/pass/EditorUiPass.h"
#include "render/pass/ScenePass.h"
#include "render/pass/ShadowPass.h"
#include "render/resource/GpuTexture.h"

#include <array>
#include <format>

struct RenderGraph::Passes
{
    ShadowPass shadow;
    ScenePass scene;
    EditorPickingPass picking;
    EditorUiPass ui;
};

struct RenderGraph::Allocation
{
    struct Instance
    {
        vk::raii::DeviceMemory memory = nullptr;
        vk::raii::Image ownedImage = nullptr;
        vk::raii::ImageView ownedView = nullptr;
        std::vector<vk::raii::ImageView> layerViews;
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
    passes->picking.registerPass(*this);
    passes->ui.registerPass(*this);
}

void RenderGraph::reset() noexcept
{
    built = false;
    compiled = false;
    resources.clear();
    orderedPasses.clear();
    registeredPasses.clear();
    passInfos.clear();
    globalInputSlots.clear();
    globalOutputSlots.clear();
    passes.reset();
    renderer = nullptr;
    executingPass = nullptr;
    currentEditor = nullptr;
    sceneDepthFormat = vk::Format::eUndefined;
    pointShadowFormat = vk::Format::eUndefined;
}

void RenderGraph::build()
{
    DCHECK(renderer && passes, "RenderGraph must be initialized before build");
    renderer->waitIdle();
    built = false;
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
    built = true;
}

void RenderGraph::validateCompilation()
{
    const auto errors = compile();
    std::string message;
    for (const auto& error : errors) message += "\n  " + error;
    DCHECK(errors.empty(), "RenderGraph compilation failed:{}", message);
}

void RenderGraph::bindSceneTextures(const SceneAsset& scene)
{
    DCHECK(passes);
    passes->scene.bindSceneTextures(scene);
    built = false;
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
            if (desc.arrayLayers > 1)
            {
                for (uint32_t layer = 0; layer < desc.arrayLayers; ++layer)
                {
                    instance.layerViews.push_back(vkCheck(device.createImageView({
                        .image = *instance.ownedImage,
                        .viewType = vk::ImageViewType::e2D,
                        .format = desc.format,
                        .subresourceRange = {.aspectMask = desc.aspect,
                            .levelCount = 1, .baseArrayLayer = layer, .layerCount = 1},
                    })));
                }
            }
        }
    }
}

void RenderGraph::importSwapchain(ResourceHandle input, const ResourceDesc& use)
{
    ResourceDesc desc = use;
    desc.format = swapchain().surfaceFormat().format;
    const auto extent = swapchain().extent();
    desc.extent = vk::Extent3D{extent.width, extent.height, 1};
    desc.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
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
    DCHECK(compiled && globalInputSlots.at(input).resource != invalidHandle,
        "resource access requires a compiled and bound input slot");
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

vk::ImageView RenderGraph::layerView(ResourceHandle input, uint32_t layer) const
{
    const auto& resource = resources.at(globalInputSlots.at(input).resource);
    return *allocation(input).instances.at(instanceIndex(resource, currentFrame)).layerViews.at(layer);
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

void RenderGraph::useOutput(ResourceHandle output)
{
    const Slot& slot = globalOutputSlots.at(output);
    DCHECK(slot.owner == executingPass, "only the executing pass may apply its output state");
    const auto& resource = resources.at(slot.resource);
    const auto& previous = resource.allocation->instances.at(instanceIndex(resource, currentFrame)).state;
    if (previous.stages == slot.desc.stages && previous.access == slot.desc.access &&
        (resource.merged.type == ResourceType::Buffer || previous.layout == slot.desc.layout))
        return;

    useResource(slot.resource, slot.desc);
}

void RenderGraph::prepareRenderData(uint32_t frameIndex)
{
    DCHECK(built && compiled, "RenderGraph must be built before preparing render data");
    passes->scene.prepareRenderData(frameContext(frameIndex));
}

void RenderGraph::execute(const EditorFrameInput& editor,
    uint32_t frameIndex, uint32_t imageIndex)
{
    DCHECK(built && compiled, "RenderGraph must be built before execution");
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
            state.stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            state.access = {};
        }
    }

    vkCheck(commands().reset());
    vkCheck(commands().begin({}));
    for (PassInfo* pass : orderedPasses)
    {
        executingPass = pass;
        for (ResourceHandle input : pass->inputResourceHandles)
        {
            const Slot& slot = globalInputSlots[input];
            if (slot.binding != Binding::Ignored) useResource(slot.resource, slot.desc);
        }
        pass->executePass(*this);
        for (ResourceHandle output : pass->outputResourceHandles) useOutput(output);
    }
    for (ResourceHandle index = 0; index < resources.size(); ++index)
    {
        if (resources[index].swapchainIndexed)
            useResource(index, {.layout = vk::ImageLayout::ePresentSrcKHR});
    }
    vkCheck(commands().end());
    executingPass = nullptr;
    currentEditor = nullptr;
}

uint32_t RenderGraph::readSelectionId(uint32_t frameIndex)
{
    DCHECK(passEnabled("editor_picking"), "picking pass is disabled");
    return passes->picking.readSelectionId(frameIndex);
}

VulkanContext& RenderGraph::vulkan() const { return renderer->vulkan; }
Swapchain& RenderGraph::swapchain() const { return renderer->swapchain; }
PipelineManager& RenderGraph::pipelines() const { return renderer->pipelines; }
ShaderManager& RenderGraph::shaders() const { return renderer->shaders; }
RenderResourceManager& RenderGraph::assetResources() const { return renderer->resources; }
FrameContext& RenderGraph::frameContext(uint32_t index) const { return renderer->frames.at(index); }
const GpuScene& RenderGraph::scene() const { return renderer->scene; }
vk::Format RenderGraph::depthFormat() const { return sceneDepthFormat; }
vk::Format RenderGraph::shadowFormat() const { return pointShadowFormat; }
vk::raii::CommandBuffer& RenderGraph::commands() const { return frameContext(currentFrame).commandBufferHandle(); }
uint32_t RenderGraph::frameIndex() const { return currentFrame; }
const EditorFrameInput& RenderGraph::editorInput() const
{
    DCHECK(currentEditor);
    return *currentEditor;
}
