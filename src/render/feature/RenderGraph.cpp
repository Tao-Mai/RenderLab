#include "render/feature/RenderGraph.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/Renderer.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/feature/PointShadowPass.h"
#include "render/feature/ScenePass.h"
#include "render/feature/editor/EditorPickingCopyPass.h"
#include "render/feature/editor/EditorPickingPass.h"
#include "render/feature/editor/EditorUiPass.h"
#include "render/feature/editor/LightMarkerPass.h"
#include "render/raytracing/RayTracingBlitPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "render/resource/Buffer.h"

#include <algorithm>
#include <meta>
#include <tuple>
#include <type_traits>
#include <utility>

namespace
{
vk::ImageAspectFlags imageAspect(vk::Format format)
{
    return format == vk::Format::eD32Sfloat ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;
}
}  // namespace

struct RenderGraph::Passes
{
    template <class T>
    struct Entry
    {
        T        pass;
        PassNode node;
    };

    template <class... Ts>
    using PassTypeList = std::tuple<Entry<Ts>...>;

    PassTypeList<
        PointShadowPass,
        ScenePass,
        LightMarkerPass,
        RayTracingPass,
        RayTracingBlitPass,
        EditorPickingPass,
        EditorPickingCopyPass,
        EditorUiPass,
        EndPass>
        data;

    template <class T>
    auto& get()
    {
        return std::get<Entry<T>>(data);
    }
};

template <class T>
void RenderGraph::collectDependencies(PassNode& node, const T& value)
{
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, ImageHandle> || std::is_same_v<U, BufferHandle>)
    {
        if (value.id == kInvalidHandle)
            return;

        DCHECK(value.id >= 0 && value.id < resources.size());
        if (std::ranges::find(node.dependency, value.id) == node.dependency.end())
            node.dependency.push_back(value.id);
    }
    else
    {
        static_assert(std::is_aggregate_v<U>, "pass arguments must be handles or aggregates of handles");
        constexpr auto ctx = std::meta::access_context::current();
        template for (constexpr auto member : std::define_static_array(std::meta::nonstatic_data_members_of(^^U, ctx)))
        {
            collectDependencies(node, value.[:member:]);
        }
    }
}

template <class T, class... Args>
auto RenderGraph::setupPass(Args&&... args)
{
    auto& entry = passes->get<T>();
    auto& node  = entry.node;
    node.dependency.clear();
    node.writeSrc.clear();
    node.prev.clear();
    node.next.clear();
    node.uses.clear();
    node.execute = [pass = &entry.pass](RenderGraph& graph) { pass->execute(graph); };
    (collectDependencies(node, args), ...);
    nodes.push_back(&node);

    auto* previousNode = setupNode;
    setupNode          = &node;
    auto output        = entry.pass.setup(*this, std::forward<Args>(args)...);
    setupNode          = previousNode;

    for (ResourceHandle source : node.writeSrc)
        if (std::ranges::find(node.dependency, source) == node.dependency.end())
            node.dependency.push_back(source);

    return output;
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

    passes = std::make_shared<Passes>();
}

void RenderGraph::releaseResources() noexcept
{
    resources.clear();
    for (auto* node : nodes)
        *node = {};
    nodes.clear();
    sortedNodes.clear();

    setupNode       = nullptr;
    swapchainImage  = {};
    pickingReadback = {};
}

void RenderGraph::reset() noexcept
{
    releaseResources();
    passes.reset();
    renderer      = nullptr;
    currentEditor = nullptr;
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
        const auto shadow  = setupPass<PointShadowPass>();
        const auto scene   = setupPass<ScenePass>(color, shadow);
        const auto markers = setupPass<LightMarkerPass>(scene.color, scene.depth);
        sceneColor         = markers.color;
        sceneDepth         = markers.depth;
    }

    const auto [pickingImage] = setupPass<EditorPickingPass>(sceneDepth);
    const auto [readback]     = setupPass<EditorPickingCopyPass>(pickingImage);
    const auto [finalColor]   = setupPass<EditorUiPass>(sceneColor);
    pickingReadback           = readback;

    setupPass<EndPass>(finalColor, readback);

    allocateResources();
    if (!rayTracing)
        passes->get<ScenePass>().pass.bindResources(*this);
}

void RenderGraph::allocateResources()
{
    const auto& device         = vulkan().deviceHandle();
    const auto& physicalDevice = vulkan().physicalDeviceHandle();
    for (Resource& entry : resources)
    {
        if (!entry.data)
            continue;

        auto& resource      = *entry.data;
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

    const auto extent   = swapchain().extent();
    auto       resource = std::make_unique<ResourceData>(ResourceData{
              .imageInfo = {.format = swapchain().surfaceFormat().format, .extent = {extent.width, extent.height, 1}},
              .swapchainIndexed = true,
    });
    for (uint32_t i = 0; i < swapchain().imageCount(); ++i)
        resource->externalImages.push_back(swapchain().image(i));
    swapchainImage = {static_cast<ResourceHandle>(resources.size())};
    resources.push_back({.data = std::move(resource)});
    return swapchainImage;
}

uint32_t RenderGraph::instanceIndex(const ResourceData& resource, uint32_t frameIndex) const
{
    if (resource.swapchainIndexed)
        return currentImage;
    return resource.allocation->instances.size() == 1 ? 0 : frameIndex;
}

Image& RenderGraph::getImage(ImageHandle image) { return getImage(image, currentFrame); }

Image& RenderGraph::getImage(ImageHandle image, uint32_t frameIndex)
{
    const auto& resource = resolveResource(image.id);
    DCHECK(!resource.isBuffer && resource.allocation);
    return *resource.allocation->instances.at(instanceIndex(resource, frameIndex)).image;
}

Buffer& RenderGraph::getBuffer(BufferHandle buffer) { return getBuffer(buffer, currentFrame); }

Buffer& RenderGraph::getBuffer(BufferHandle buffer, uint32_t frameIndex)
{
    const auto& resource = resolveResource(buffer.id);
    DCHECK(resource.isBuffer && resource.allocation);
    return *resource.allocation->instances.at(instanceIndex(resource, frameIndex)).buffer;
}

void RenderGraph::useResource(const ResourceUse& use)
{
    auto&      resource = resolveResource(use.resource);
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
    DCHECK(!sortedNodes.empty());

    // TODO
    currentFrame  = frameIndex;
    currentImage  = imageIndex;
    currentEditor = &editor;
    for (Resource& entry : resources)
    {
        if (!entry.data)
            continue;

        auto& resource = *entry.data;
        if (resource.swapchainIndexed)
        {
            auto& state  = resource.allocation->instances.at(currentImage).state;
            state.stages = resource.initialState.stages;
            state.access = {};
        }
    }

    vkCheck(commands().reset());
    vkCheck(commands().begin({}));
    for (auto* node : sortedNodes)
    {
        transitionResources(node->uses);
        node->execute(*this);
    }

    vkCheck(commands().end());
    currentEditor = nullptr;
}

uint32_t RenderGraph::readSelectionId(uint32_t frameIndex)
{
    uint32_t selectionId = 0;
    getBuffer(pickingReadback, frameIndex).download(&selectionId, sizeof(selectionId));
    return selectionId;
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
