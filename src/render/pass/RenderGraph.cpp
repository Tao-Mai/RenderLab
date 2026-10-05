#include "render/pass/RenderGraph.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/Renderer.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/pass/EditorPickingCopyPass.h"
#include "render/pass/EditorPickingPass.h"
#include "render/pass/EditorPickingReadbackPass.h"
#include "render/pass/EditorUiPass.h"
#include "render/pass/PointShadowPass.h"
#include "render/pass/ScenePass.h"
#include "render/raytracing/RayTracingBlitPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/resource/GpuTexture.h"

#include <tuple>
#include <utility>

namespace
{
vk::ImageAspectFlags imageAspect(vk::Format format)
{
    return format == vk::Format::eD32Sfloat || format == vk::Format::eD16Unorm ? vk::ImageAspectFlagBits::eDepth
                                                                               : vk::ImageAspectFlagBits::eColor;
}
}  // namespace

struct RenderGraph::Passes
{
    template <class T>
    struct Entry
    {
        T                        pass;
        std::vector<ResourceUse> uses;
    };

    template <class... Ts>
    using PassTypeList = std::tuple<Entry<Ts>...>;

    PassTypeList<
        PointShadowPass,
        ScenePass,
        RayTracingPass,
        RayTracingBlitPass,
        EditorPickingPass,
        EditorPickingCopyPass,
        EditorPickingReadbackPass,
        EditorUiPass>
        data;

    template <class T>
    auto& get()
    {
        return std::get<Entry<T>>(data);
    }
};

template <class T, class... Args>
auto RenderGraph::setupPass(Args&&... args)
{
    auto& entry = passes->get<T>();
    entry.uses.clear();

    auto* previousUses = setupUses;
    setupUses          = &entry.uses;

    auto output = entry.pass.setup(*this, std::forward<Args>(args)...);
    setupUses   = previousUses;
    return output;
}

template <class T>
void RenderGraph::executePass()
{
    auto& entry = passes->get<T>();
    transitionResources(entry.uses);
    entry.pass.execute(*this);
}

struct RenderGraph::Allocation
{
    struct Instance
    {
        vk::raii::DeviceMemory  memory     = nullptr;
        vk::raii::Image         ownedImage = nullptr;
        std::unique_ptr<Buffer> buffer;
        std::unique_ptr<Image>  image;
        ImageAccess             state;
    };
    std::vector<Instance> instances;
};

void RenderGraph::init()
{
    DCHECK(!renderer);
    renderer = context().renderer;
    DCHECK(renderer);

    const auto chooseDepthFormat = [&](vk::FormatFeatureFlags features)
    {
        for (vk::Format format : {vk::Format::eD32Sfloat, vk::Format::eD16Unorm})
            if ((vulkan().physicalDeviceHandle().getFormatProperties(format).optimalTilingFeatures & features) ==
                features)
                return format;
        CHECK(false, "no depth format supports the render graph's required features");
        return vk::Format::eUndefined;
    };

    sceneDepthFormat  = chooseDepthFormat(vk::FormatFeatureFlagBits::eDepthStencilAttachment);
    pointShadowFormat = chooseDepthFormat(
        vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage
    );
    passes = std::make_shared<Passes>();
}

void RenderGraph::releaseResources() noexcept
{
    resources.clear();
    if (passes)
        std::apply([](auto&... entries) { (entries.uses.clear(), ...); }, passes->data);
    setupUses      = nullptr;
    swapchainImage = {};
}

void RenderGraph::reset() noexcept
{
    releaseResources();
    passes.reset();
    renderer          = nullptr;
    currentEditor     = nullptr;
    sceneDepthFormat  = vk::Format::eUndefined;
    pointShadowFormat = vk::Format::eUndefined;
}

void RenderGraph::setup()
{
    DCHECK(renderer && passes);
    renderer->waitIdle();
    releaseResources();

    const auto  color = importSwapchain();
    ImageHandle sceneColor;
    ImageHandle sceneDepth;
    const bool  rayTracing = context().config->rendererConfig().renderPath == RenderPath::RayTracing;
    if (rayTracing)
    {
        const auto [rayImage] = setupPass<RayTracingPass>();
        sceneColor            = setupPass<RayTracingBlitPass>(rayImage, color).color;
    }
    else
    {
        const auto shadow = setupPass<PointShadowPass>();
        const auto scene  = setupPass<ScenePass>(color, shadow);
        sceneColor        = scene.color;
        sceneDepth        = scene.depth;
    }

    const auto picking = setupPass<EditorPickingPass>(sceneDepth);
    const auto copy    = setupPass<EditorPickingCopyPass>(picking.pickingImage);
    setupPass<EditorPickingReadbackPass>(copy.readback);
    setupPass<EditorUiPass>(sceneColor);

    allocateResources();
    if (!rayTracing)
        passes->get<ScenePass>().pass.bindResources(*this);
}

void RenderGraph::allocateResources()
{
    const auto& device         = vulkan().deviceHandle();
    const auto& physicalDevice = vulkan().physicalDeviceHandle();
    for (Resource& resource : resources)
    {
        resource.allocation = std::make_shared<Allocation>();
        const size_t count  = resource.externalImages.empty() ? maxFramesInFlight : resource.externalImages.size();
        resource.allocation->instances.resize(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto& instance = resource.allocation->instances[i];
            if (!resource.externalImages.empty())
            {
                instance.image = std::make_unique<Image>(device, resource.externalImages[i]);
                instance.state = resource.initialState;
                continue;
            }
            if (resource.isBuffer)
            {
                DCHECK(resource.bufferUsage);
                instance.buffer = std::make_unique<Buffer>(
                    physicalDevice,
                    device,
                    resource.bufferInfo.size,
                    resource.bufferUsage,
                    resource.bufferInfo.memoryProperties
                );
                continue;
            }

            const auto& info = resource.imageInfo;
            DCHECK(resource.imageUsage);
            instance.ownedImage     = vkCheck(device.createImage({
                    .flags         = info.flags,
                    .imageType     = info.imageType,
                    .format        = info.format,
                    .extent        = info.extent,
                    .mipLevels     = info.mipLevels,
                    .arrayLayers   = info.arrayLayers,
                    .samples       = vk::SampleCountFlagBits::e1,
                    .tiling        = vk::ImageTiling::eOptimal,
                    .usage         = resource.imageUsage,
                    .sharingMode   = vk::SharingMode::eExclusive,
                    .initialLayout = vk::ImageLayout::eUndefined,
            }));
            const auto requirements = instance.ownedImage.getMemoryRequirements();
            instance.memory         = vkCheck(device.allocateMemory({
                        .allocationSize  = requirements.size,
                        .memoryTypeIndex = vulkan_memory::findType(
                    physicalDevice,
                    requirements.memoryTypeBits,
                    vk::MemoryPropertyFlagBits::eDeviceLocal
                ),
            }));
            vkCheck(instance.ownedImage.bindMemory(*instance.memory, 0));
            instance.image = std::make_unique<Image>(device, *instance.ownedImage);
        }
    }
}

RenderGraph::ImageHandle RenderGraph::importSwapchain()
{
    if (swapchainImage.id != kInvalidHandle)
        return swapchainImage;

    const auto extent = swapchain().extent();
    Resource   resource{
          .imageInfo = {.format = swapchain().surfaceFormat().format, .extent = {extent.width, extent.height, 1}},
          .swapchainIndexed = true,
    };
    for (uint32_t i = 0; i < swapchain().imageCount(); ++i)
        resource.externalImages.push_back(swapchain().image(i));
    swapchainImage = {static_cast<ResourceHandle>(resources.size())};
    resources.push_back(std::move(resource));
    return swapchainImage;
}

RenderGraph::ImageHandle RenderGraph::importTexture(const GpuTexture& texture)
{
    for (ResourceHandle id = 0; id < static_cast<ResourceHandle>(resources.size()); ++id)
    {
        const auto& images = resources[id].externalImages;
        if (images.size() == 1 && images.front() == texture.imageHandle())
            return {id};
    }

    const auto& info = texture.imageInfo();
    const auto  id   = static_cast<ResourceHandle>(resources.size());
    resources.push_back({
        .imageInfo =
            {.flags       = info.flags,
             .imageType   = info.imageType,
             .format      = info.format,
             .extent      = info.extent,
             .mipLevels   = info.mipLevels,
             .arrayLayers = info.arrayLayers},
        .externalImages = {texture.imageHandle()},
        .initialState   = {
              .stages = vk::PipelineStageFlagBits2::eFragmentShader,
              .access = vk::AccessFlagBits2::eShaderSampledRead,
              .layout = vk::ImageLayout::eShaderReadOnlyOptimal
        },
    });
    return {id};
}

uint32_t RenderGraph::instanceIndex(const Resource& resource, uint32_t frameIndex) const
{
    if (resource.swapchainIndexed)
        return currentImage;
    return resource.allocation->instances.size() == 1 ? 0 : frameIndex;
}

Image& RenderGraph::getImage(ImageHandle image) { return getImage(image, currentFrame); }

Image& RenderGraph::getImage(ImageHandle image, uint32_t frameIndex)
{
    const auto& resource = resources.at(image.id);
    DCHECK(!resource.isBuffer && resource.allocation);
    return *resource.allocation->instances.at(instanceIndex(resource, frameIndex)).image;
}

Buffer& RenderGraph::getBuffer(BufferHandle buffer) { return getBuffer(buffer, currentFrame); }

Buffer& RenderGraph::getBuffer(BufferHandle buffer, uint32_t frameIndex)
{
    const auto& resource = resources.at(buffer.id);
    DCHECK(resource.isBuffer && resource.allocation);
    return *resource.allocation->instances.at(instanceIndex(resource, frameIndex)).buffer;
}

void RenderGraph::useResource(const ResourceUse& use)
{
    Resource&  resource = resources.at(use.resource);
    auto&      instance = resource.allocation->instances.at(instanceIndex(resource, currentFrame));
    const auto previous = instance.state;
    const auto next     = use.access;
    if (!needsBarrier(previous, next))
    {
        instance.state.stages |= next.stages;
        instance.state.access |= next.access;
        return;
    }

    if (resource.isBuffer)
    {
        const vk::BufferMemoryBarrier2 barrier{
            .srcStageMask        = previous.stages,
            .srcAccessMask       = previous.access,
            .dstStageMask        = next.stages,
            .dstAccessMask       = next.access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = instance.buffer->handle(),
            .size                = resource.bufferInfo.size,
        };
        commands().pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
    }
    else
    {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask        = previous.stages,
            .srcAccessMask       = previous.access,
            .dstStageMask        = next.stages,
            .dstAccessMask       = next.access,
            .oldLayout           = previous.layout,
            .newLayout           = next.layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = instance.image->handle(),
            .subresourceRange    = {
                   .aspectMask = imageAspect(resource.imageInfo.format),
                   .levelCount = resource.imageInfo.mipLevels,
                   .layerCount = resource.imageInfo.arrayLayers
            },
        };
        commands().pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier});
    }
    instance.state = next;
}

void RenderGraph::transitionResources(const std::vector<ResourceUse>& uses)
{
    for (const ResourceUse& use : uses)
        useResource(use);
}

void RenderGraph::execute(const EditorFrameInput& editor, uint32_t frameIndex, uint32_t imageIndex)
{
    currentFrame          = frameIndex;
    currentImage          = imageIndex;
    currentEditor         = &editor;
    const bool rayTracing = context().config->rendererConfig().renderPath == RenderPath::RayTracing;
    for (Resource& resource : resources)
    {
        if (resource.swapchainIndexed)
        {
            auto& state = resource.allocation->instances.at(currentImage).state;
            state.stages =
                rayTracing ? vk::PipelineStageFlagBits2::eTransfer : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            state.access = {};
        }
    }

    vkCheck(commands().reset());
    vkCheck(commands().begin({}));
    if (rayTracing)
    {
        executePass<RayTracingPass>();

        executePass<RayTracingBlitPass>();
    }
    else
    {
        executePass<PointShadowPass>();

        executePass<ScenePass>();
    }

    if (editor.requestPick)
    {
        executePass<EditorPickingPass>();

        executePass<EditorPickingCopyPass>();

        executePass<EditorPickingReadbackPass>();
    }

    executePass<EditorUiPass>();
    useResource({swapchainImage.id, {.layout = vk::ImageLayout::ePresentSrcKHR}});
    vkCheck(commands().end());
    currentEditor = nullptr;
}

uint32_t RenderGraph::readSelectionId(uint32_t frameIndex)
{
    return passes->get<EditorPickingReadbackPass>().pass.readSelectionId(*this, frameIndex);
}

VulkanContext&           RenderGraph::vulkan() const { return renderer->vulkan; }
Swapchain&               RenderGraph::swapchain() const { return renderer->swapchain; }
FrameData&               RenderGraph::frameData(uint32_t index) const { return renderer->frames.at(index).data; }
const RenderData&        RenderGraph::renderData() const { return renderer->renderSystem.renderData(); }
const LightMarkers&      RenderGraph::lightMarkers() const { return renderer->renderSystem.lightMarkers(); }
vk::raii::CommandBuffer& RenderGraph::commands() const
{
    return renderer->frames.at(currentFrame).commandBufferHandle();
}
