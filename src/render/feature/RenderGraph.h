#pragma once

#include "core/Logger.h"
#include "render/Image.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <meta>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

class Buffer;
class FrameData;
class FrameContext;
class Swapchain;
class VulkanContext;
struct RenderData;
struct RenderView;
struct EditorFrameInput;

class RenderGraph
{
public:
    using ResourceHandle                           = int;
    static constexpr ResourceHandle kInvalidHandle = -1;

    struct ImageHandle
    {
        ResourceHandle id = kInvalidHandle;
    };
    struct BufferHandle
    {
        ResourceHandle id = kInvalidHandle;
    };

    struct ImageCreateInfo
    {
        vk::ImageCreateFlags flags;
        vk::ImageType        imageType = vk::ImageType::e2D;
        vk::Format           format    = vk::Format::eUndefined;
        vk::Extent3D         extent{0, 0, 1};
        uint32_t             mipLevels   = 1;
        uint32_t             arrayLayers = 1;
    };

    struct BufferCreateInfo
    {
        vk::DeviceSize          size             = 0;
        vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    };

    struct ImageAccess
    {
        vk::PipelineStageFlags2 stages;
        vk::AccessFlags2        access;
        vk::ImageLayout         layout = vk::ImageLayout::eUndefined;
    };

    struct BufferAccess
    {
        vk::PipelineStageFlags2 stages;
        vk::AccessFlags2        access;
    };

    void init(VulkanContext& vulkan, Swapchain& swapchain, std::span<FrameContext> frames, std::span<FrameData> frameData);
    void reset() noexcept;
    void compile();
    void releaseResources() noexcept;

    template <class T, class... Args>
    auto addPass(T& pass, Args&&... args);

    ImageHandle  createImage(ImageCreateInfo info);
    BufferHandle createBuffer(BufferCreateInfo info);
    ImageHandle importSwapchain();
    // Borrowed resources must outlive the graph and all submitted frames.
    // Owners provide the initial access; graph preserves state between executions.
    ImageHandle importImage(Image& image, ImageCreateInfo info, ImageAccess initial);
    BufferHandle importBuffer(Buffer& buffer, BufferAccess initial);
    [[nodiscard]] ImageAccess imageState(ImageHandle image, uint32_t frameIndex) const;
    [[nodiscard]] BufferAccess bufferState(BufferHandle buffer, uint32_t frameIndex) const;
    ImageHandle  write(ImageHandle image);
    BufferHandle write(BufferHandle buffer);
    void         use(ImageHandle image, ImageAccess access);
    void         use(BufferHandle buffer, BufferAccess access);

    [[nodiscard]] Image&  getImage(ImageHandle image);
    [[nodiscard]] Image&  getImage(ImageHandle image, uint32_t frameIndex);
    [[nodiscard]] Buffer& getBuffer(BufferHandle buffer);
    [[nodiscard]] Buffer& getBuffer(BufferHandle buffer, uint32_t frameIndex);
    [[nodiscard]] const ImageCreateInfo& imageInfo(ImageHandle image);

    void execute(
        const RenderView& view,
        const RenderData& renderData,
        const EditorFrameInput& editor,
        uint32_t frameIndex,
        uint32_t imageIndex
    );
    [[nodiscard]] static bool needsBarrier(const ImageAccess& previous, const ImageAccess& next);

    [[nodiscard]] VulkanContext& vulkan() const { return *deviceContext; }
    [[nodiscard]] Swapchain&     swapchain() const { return *targetSwapchain; }
    [[nodiscard]] FrameData&     frameData(uint32_t frameIndex) const;
    [[nodiscard]] const RenderData& renderData() const
    {
        DCHECK(currentRenderData);
        return *currentRenderData;
    }
    [[nodiscard]] const RenderView& view() const
    {
        DCHECK(currentView);
        return *currentView;
    }
    [[nodiscard]] vk::raii::CommandBuffer& commands() const;
    [[nodiscard]] uint32_t                 frameIndex() const { return currentFrame; }
    [[nodiscard]] const EditorFrameInput&  editorInput() const
    {
        DCHECK(currentEditor);
        return *currentEditor;
    }

private:
    struct PassNode;
    struct Allocation;
    struct ResourceUse
    {
        ResourceHandle resource;
        ImageAccess    access;
    };
    struct ResourceData
    {
        bool                        isBuffer = false;
        ImageCreateInfo             imageInfo;
        BufferCreateInfo            bufferInfo;
        vk::ImageUsageFlags         imageUsage;
        vk::BufferUsageFlags        bufferUsage;
        std::vector<vk::Image>      externalImages;
        Image* importedImage = nullptr;
        Buffer* importedBuffer = nullptr;
        ImageAccess                 initialState;
        bool                        swapchainIndexed = false;
        std::shared_ptr<Allocation> allocation;
    };
    struct Resource
    {
        // Root resources own data; written versions refer to the previous handle.
        std::unique_ptr<ResourceData> data;
        ResourceHandle                previous = kInvalidHandle;
        PassNode*                     owner    = nullptr;
    };

    struct PassNode
    {
        std::vector<ResourceHandle>       dependency;
        std::vector<ResourceHandle>       writeSrc;
        std::vector<PassNode*>            prev;
        std::vector<PassNode*>            next;
        std::vector<ResourceUse>          uses;
        std::function<void(RenderGraph&)> prepare;
        std::function<void(RenderGraph&)> execute;
    };

    VulkanContext*                       deviceContext   = nullptr;
    Swapchain*                           targetSwapchain = nullptr;
    std::span<FrameContext> frames;
    std::span<FrameData> frameStorage;
    std::vector<std::unique_ptr<PassNode>> nodeStorage;
    std::vector<PassNode*>                nodes;
    std::vector<PassNode*>                sortedNodes;
    PassNode*                            setupNode = nullptr;
    std::vector<Resource>                resources;
    uint32_t                             currentFrame      = 0;
    uint32_t                             currentImage      = 0;
    const EditorFrameInput*               currentEditor     = nullptr;
    const RenderData*                     currentRenderData = nullptr;
    const RenderView*                     currentView       = nullptr;
    ImageHandle                          swapchainImage;

    template <class T>
    void collectDependencies(PassNode& node, const T& value);

    void                   compileNodes();
    void                   allocateResources();
    ResourceData&          resolveResource(ResourceHandle handle);
    const ResourceData& resolveResource(ResourceHandle handle) const;
    void                   useResource(const ResourceUse& use);
    void                   transitionResources(const std::vector<ResourceUse>& uses);
    [[nodiscard]] uint32_t instanceIndex(const ResourceData& resource, uint32_t frameIndex) const;
};

template <class T, class... Args>
auto RenderGraph::addPass(T& pass, Args&&... args)
{
    auto& node   = *nodeStorage.emplace_back(std::make_unique<PassNode>());
    node.execute = [pass = &pass](RenderGraph& graph) { pass->execute(graph); };
    if constexpr (requires { pass.prepare(*this); })
        node.prepare = [pass = &pass](RenderGraph& graph) { pass->prepare(graph); };
    (collectDependencies(node, args), ...);
    nodes.push_back(&node);

    auto* previousNode = setupNode;
    setupNode          = &node;
    auto output        = pass.setup(*this, std::forward<Args>(args)...);
    setupNode          = previousNode;

    for (ResourceHandle source : node.writeSrc)
        if (std::ranges::find(node.dependency, source) == node.dependency.end())
            node.dependency.push_back(source);

    return output;
}

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
