#pragma once

#include "core/Logger.h"
#include "render/Image.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class Buffer;
class FrameData;
class LightMarkers;
class Renderer;
class Swapchain;
class VulkanContext;
struct RenderData;
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

    void init();
    void reset() noexcept;
    void setup();
    void compile();
    void releaseResources() noexcept;

    ImageHandle  createImage(ImageCreateInfo info);
    BufferHandle createBuffer(BufferCreateInfo info);
    ImageHandle  importSwapchain();
    ImageHandle  write(ImageHandle image);
    BufferHandle write(BufferHandle buffer);
    void         use(ImageHandle image, ImageAccess access);
    void         use(BufferHandle buffer, BufferAccess access);

    [[nodiscard]] Image&  getImage(ImageHandle image);
    [[nodiscard]] Image&  getImage(ImageHandle image, uint32_t frameIndex);
    [[nodiscard]] Buffer& getBuffer(BufferHandle buffer);
    [[nodiscard]] Buffer& getBuffer(BufferHandle buffer, uint32_t frameIndex);

    void                      execute(const EditorFrameInput& editor, uint32_t frameIndex, uint32_t imageIndex);
    [[nodiscard]] uint32_t    readSelectionId(uint32_t frameIndex);
    [[nodiscard]] static bool needsBarrier(const ImageAccess& previous, const ImageAccess& next);

    [[nodiscard]] VulkanContext&           vulkan() const;
    [[nodiscard]] Swapchain&               swapchain() const;
    [[nodiscard]] FrameData&               frameData(uint32_t frameIndex) const;
    [[nodiscard]] const RenderData&        renderData() const;
    [[nodiscard]] const LightMarkers&      lightMarkers() const;
    [[nodiscard]] vk::raii::CommandBuffer& commands() const;
    [[nodiscard]] uint32_t                 frameIndex() const { return currentFrame; }
    [[nodiscard]] const EditorFrameInput&  editorInput() const
    {
        DCHECK(currentEditor);
        return *currentEditor;
    }

private:
    struct Passes;
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
        std::function<void(RenderGraph&)> execute;
    };

    Renderer*               renderer = nullptr;
    std::shared_ptr<Passes> passes;
    std::vector<PassNode*>  nodes;
    std::vector<PassNode*>  sortedNodes;
    PassNode*               setupNode = nullptr;
    std::vector<Resource>   resources;
    uint32_t                currentFrame  = 0;
    uint32_t                currentImage  = 0;
    const EditorFrameInput* currentEditor = nullptr;
    ImageHandle             swapchainImage;
    BufferHandle            pickingReadback;

    template <class T, class... Args>
    auto setupPass(Args&&... args);

    template <class T>
    void collectDependencies(PassNode& node, const T& value);

    void                   allocateResources();
    ResourceData&          resolveResource(ResourceHandle handle);
    void                   useResource(const ResourceUse& use);
    void                   transitionResources(const std::vector<ResourceUse>& uses);
    [[nodiscard]] uint32_t instanceIndex(const ResourceData& resource, uint32_t frameIndex) const;
};
