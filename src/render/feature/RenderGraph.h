#pragma once

#include "core/Logger.h"
#include "core/profiling/Profiler.h"
#include "render/Image.h"
#include "render/device/CommandRecorder.h"
#include "render/feature/PassInputs.h"

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
class GpuProfiler;
class Swapchain;
class VulkanContext;

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
    struct AccelerationStructureHandle
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

    struct MemoryAccess
    {
        vk::PipelineStageFlags2 stages;
        vk::AccessFlags2        access;
    };

    void init(VulkanContext& vulkan, Swapchain& swapchain);
    void reset() noexcept;
    void compile(const PassSetup& setup);
    void releaseResources() noexcept;

    template <class T, class... Args>
    auto addPass(T& pass, const PassSetup& setup, Args&&... args);

    ImageHandle createImage(ImageCreateInfo info);
    BufferHandle createBuffer(BufferCreateInfo info);

    // Tracks a pass-owned AS or BLAS collection; allocation remains with the owner.
    AccelerationStructureHandle createAccelerationStructure(MemoryAccess initial);
    ImageHandle importSwapchain();

    // Borrowed resources must outlive the graph and all submitted frames.
    // Owners provide the initial access; graph preserves state between executions.
    ImageHandle importImage(Image& image, ImageCreateInfo info, ImageAccess initial);
    BufferHandle importBuffer(Buffer& buffer, MemoryAccess initial);
    [[nodiscard]] ImageAccess imageState(ImageHandle image, uint32_t frameIndex) const;
    [[nodiscard]] MemoryAccess bufferState(BufferHandle buffer, uint32_t frameIndex) const;

    ImageHandle write(ImageHandle image);
    BufferHandle write(BufferHandle buffer);
    AccelerationStructureHandle write(AccelerationStructureHandle structure);

    void use(ImageHandle image, ImageAccess access);
    void use(BufferHandle buffer, MemoryAccess access);
    void use(AccelerationStructureHandle structure, MemoryAccess access);

    [[nodiscard]] Image&                 getImage(ImageHandle image, uint32_t frameIndex);
    [[nodiscard]] Buffer&                getBuffer(BufferHandle buffer, uint32_t frameIndex);
    [[nodiscard]] const ImageCreateInfo& imageInfo(ImageHandle image);

    void execute(
        CommandRecorder& commandBuffer,
        const PassFrame& frame,
        uint32_t         imageIndex,
        GpuProfiler&     gpuProfiler,
        Profiler*        profiler
    );
    [[nodiscard]] static bool needsBarrier(const ImageAccess& previous, const ImageAccess& next);

private:
    enum class ResourceType
    {
        Image,
        Buffer,
        AccelerationStructure
    };
    struct PassNode;
    struct Allocation;
    struct ResourceUse
    {
        ResourceHandle resource;
        ImageAccess    access;
    };
    struct ResourceData
    {
        ResourceType                type = ResourceType::Image;
        ImageCreateInfo             imageInfo;
        BufferCreateInfo            bufferInfo;
        vk::ImageUsageFlags         imageUsage;
        vk::BufferUsageFlags        bufferUsage;
        std::vector<vk::Image>       externalImages;
        Image*                      importedImage = nullptr;
        Buffer*                     importedBuffer = nullptr;
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
        std::string_view                  name;
        std::vector<ResourceHandle>       dependency;
        std::vector<ResourceHandle>       writeSrc;
        std::vector<PassNode*>            prev;
        std::vector<PassNode*>            next;
        std::vector<ResourceUse>          uses;
        std::function<void(RenderGraph&, const PassSetup&)>                  prepare;
        std::function<void(RenderGraph&, CommandRecorder&, const PassFrame&)> execute;
    };

    VulkanContext*                       deviceContext   = nullptr;
    Swapchain*                           targetSwapchain = nullptr;
    std::vector<std::unique_ptr<PassNode>> nodeStorage;
    std::vector<PassNode*>                nodes;
    std::vector<PassNode*>                sortedNodes;
    PassNode*                            setupNode       = nullptr;
    std::vector<Resource>                resources;
    uint32_t                             currentImage    = 0;
    ImageHandle                          swapchainImage;

    template <class T>
    void collectDependencies(PassNode& node, const T& value);

    void                compileNodes();
    void                allocateResources();
    ResourceData&       resolveResource(ResourceHandle handle);
    const ResourceData& resolveResource(ResourceHandle handle) const;
    void useResource(CommandRecorder& commandBuffer, const ResourceUse& use, uint32_t frameIndex);
    void transitionResources(
        CommandRecorder& commandBuffer, const std::vector<ResourceUse>& uses, uint32_t frameIndex
    );
    [[nodiscard]] uint32_t instanceIndex(const ResourceData& resource, uint32_t frameIndex) const;
};

template <class T, class... Args>
auto RenderGraph::addPass(T& pass, const PassSetup& setup, Args&&... args)
{
    if constexpr (!requires(CommandRecorder& commandBuffer, const PassFrame& frame)
                  { pass.execute(*this, commandBuffer, frame); })
    {
        // Setup-only passes register their executable nodes internally.
        return pass.setup(*this, setup, std::forward<Args>(args)...);
    }
    else
    {
        auto& node   = *nodeStorage.emplace_back(std::make_unique<PassNode>());
        node.name    = std::define_static_string(std::meta::identifier_of(^^T));
        node.execute = [pass = &pass](RenderGraph& graph, CommandRecorder& commandBuffer, const PassFrame& frame)
        {
            pass->execute(graph, commandBuffer, frame);
        };
        if constexpr (requires { pass.prepare(*this, setup); })
            node.prepare = [pass = &pass](RenderGraph& graph, const PassSetup& setup) { pass->prepare(graph, setup); };
        (collectDependencies(node, args), ...);
        nodes.push_back(&node);

        auto* previousNode = setupNode;
        setupNode          = &node;
        auto output        = pass.setup(*this, setup, std::forward<Args>(args)...);
        setupNode          = previousNode;

        for (ResourceHandle source : node.writeSrc)
            if (std::ranges::find(node.dependency, source) == node.dependency.end())
                node.dependency.push_back(source);

        return output;
    }
}

template <class T>
void RenderGraph::collectDependencies(PassNode& node, const T& value)
{
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, ImageHandle> || std::is_same_v<U, BufferHandle> ||
                  std::is_same_v<U, AccelerationStructureHandle>)
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
