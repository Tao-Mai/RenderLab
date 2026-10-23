#pragma once

#include "core/Logger.h"
#include "core/profiling/Profiler.h"
#include "render/Image.h"
#include "render/feature/PassInputs.h"
#include "render/RenderConfig.h"
#include "render/resource/Buffer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <meta>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class AccelerationStructure;
class GpuProfiler;
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
        vk::BufferUsageFlags    usage;
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

    void init(VulkanContext& vulkan);
    void reset() noexcept;
    void compile(const PassSetup& setup);
    // Call after the frame fence completes, before global/pass preparation.
    void beginFrame(uint32_t frameIndex);
    void prepare(const PassFrame& frame);
    void releaseResources() noexcept;

    template <class T, class... Args>
    auto addPass(T& pass, const PassSetup& setup, Args&&... args);

    ImageHandle createImage(ImageCreateInfo info);
    // Declare the format and extent at setup; bind the actual image before execution.
    ImageHandle createExternalImage(ImageCreateInfo info);
    void bindImage(ImageHandle handle, Image& image);
    BufferHandle createBuffer(BufferCreateInfo info);

    struct BufferUpload
    {
        vk::DeviceSize       offset;
        std::span<std::byte> data;
    };
    // Writable staging storage; the matching GPU copy is recorded during execute.
    [[nodiscard]] BufferUpload
    allocateUpload(BufferHandle buffer, vk::DeviceSize size, vk::DeviceSize destinationOffset = 0);

    // Tracks a pass-owned AS or BLAS collection; allocation remains with the owner.
    AccelerationStructureHandle createAccelerationStructure(MemoryAccess initial);

    [[nodiscard]] uint64_t bufferRevision(BufferHandle buffer) const;

    ImageHandle write(ImageHandle image);
    BufferHandle write(BufferHandle buffer);
    AccelerationStructureHandle write(AccelerationStructureHandle structure);

    void use(ImageHandle image, ImageAccess access);
    void use(BufferHandle buffer, MemoryAccess access);
    void use(AccelerationStructureHandle structure, MemoryAccess access);

    [[nodiscard]] Image&                 getImage(ImageHandle image);
    [[nodiscard]] Buffer&                getBuffer(BufferHandle buffer);
    // Bind pass-owned objects for downstream lookup; the pass retains ownership.
    void bindAccelerationStructures(
        AccelerationStructureHandle handle, std::span<AccelerationStructure* const> structures
    );
    [[nodiscard]] AccelerationStructure&
    getAccelerationStructure(AccelerationStructureHandle handle, size_t index = 0);
    [[nodiscard]] const ImageCreateInfo& imageInfo(ImageHandle image);
    [[nodiscard]] vk::Extent2D imageExtent(ImageHandle image);
    [[nodiscard]] vk::PipelineStageFlags2 firstUseStages(ImageHandle image) const;

    void execute(
        vk::raii::CommandBuffer& commandBuffer,
        const PassFrame& frame,
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
    struct StagingBlock
    {
        Buffer         buffer;
        std::byte*     mapped = nullptr;
        vk::DeviceSize used   = 0;
    };
    struct BufferCopy
    {
        BufferHandle   destination;
        size_t         blockIndex;
        vk::BufferCopy region;
    };
    struct StagingPool
    {
        std::vector<StagingBlock> blocks;
        size_t                   blockIndex = 0;
        std::vector<BufferCopy> copies;
    };
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
        std::vector<AccelerationStructure*> accelerationStructures;
        ImageAccess                 initialState;
        vk::PipelineStageFlags2     firstUseStages;
        bool                        externalImage = false;
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
        std::function<void(RenderGraph&, const PassSetup&)>                  writeDescriptorSet;
        std::function<void(RenderGraph&, const PassFrame&)> prepare;
        std::function<void(RenderGraph&, vk::raii::CommandBuffer&, const PassFrame&)> execute;
    };

    VulkanContext*                       deviceContext   = nullptr;
    std::vector<std::unique_ptr<PassNode>> nodeStorage;
    std::vector<PassNode*>                nodes;
    std::vector<PassNode*>                sortedNodes;
    PassNode*                            setupNode       = nullptr;
    std::vector<Resource>                resources;
    std::array<StagingPool, maxFramesInFlight> stagingPools;
    uint32_t currentFrameIndex = 0;

    template <class T>
    void collectDependencies(PassNode& node, const T& value);

    void                compileNodes();
    void                allocateResources();
    void recordUploads(vk::raii::CommandBuffer& commandBuffer);
    ResourceData&       resolveResource(ResourceHandle handle);
    const ResourceData& resolveResource(ResourceHandle handle) const;
    void useResource(vk::raii::CommandBuffer& commandBuffer, const ResourceUse& use);
    void transitionResources(
        vk::raii::CommandBuffer& commandBuffer, const std::vector<ResourceUse>& uses
    );
};

template <class T, class... Args>
auto RenderGraph::addPass(T& pass, const PassSetup& setup, Args&&... args)
{
    if constexpr (!requires(vk::raii::CommandBuffer& commandBuffer, const PassFrame& frame)
                  { pass.execute(*this, commandBuffer, frame); })
    {
        // Setup-only passes register their executable nodes internally.
        return pass.setup(*this, setup, std::forward<Args>(args)...);
    }
    else
    {
        auto& node   = *nodeStorage.emplace_back(std::make_unique<PassNode>());
        node.name    = std::define_static_string(std::meta::identifier_of(^^T));
        node.execute = [pass = &pass](RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& frame)
        {
            pass->execute(graph, commandBuffer, frame);
        };
        if constexpr (requires { pass.writeDescriptorSet(*this, setup); })
            node.writeDescriptorSet = [pass = &pass](RenderGraph& graph, const PassSetup& setup)
            { pass->writeDescriptorSet(graph, setup); };
        if constexpr (requires(const PassFrame& frame) { pass.prepare(*this, frame); })
            node.prepare = [pass = &pass](RenderGraph& graph, const PassFrame& frame)
            { pass->prepare(graph, frame); };
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
