#pragma once

#include "core/Logger.h"
#include "core/profiling/Profiler.h"
#include "render/resources/Image.h"
#include "render/graph/PassInputs.h"
#include "render/RenderConfig.h"
#include "render/resources/Buffer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

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

    template <class Stage, class... Args>
    auto addStage(std::string_view name, Stage& stage, const PassSetup& setup, Args&&... args);

    void markOutput(ImageHandle image) { outputResources.push_back(image.id); }
    void markOutput(BufferHandle buffer) { outputResources.push_back(buffer.id); }
    void retainStage();

    ImageHandle createImage(ImageCreateInfo info);
    // Declare the format and extent at setup; bind the actual image before execution.
    ImageHandle createExternalImage(ImageCreateInfo info);
    void bindImage(ImageHandle handle, Image& image);
    BufferHandle createBuffer(BufferCreateInfo info);
    // Growth discards the old contents; call during preparation before updating descriptors.
    void ensureBufferCapacity(BufferHandle buffer, vk::DeviceSize size);

    struct BufferUpload
    {
        vk::DeviceSize       offset;
        std::span<std::byte> data;
    };
    // Writable staging storage; the matching GPU copy is recorded during execute.
    [[nodiscard]] BufferUpload
    allocateUpload(BufferHandle buffer, vk::DeviceSize size, vk::DeviceSize destinationOffset = 0);

    [[nodiscard]] uint64_t bufferRevision(BufferHandle buffer) const;

    ImageHandle write(ImageHandle image);
    BufferHandle write(BufferHandle buffer);

    void use(ImageHandle image, ImageAccess access);
    void use(BufferHandle buffer, MemoryAccess access);

    [[nodiscard]] Image&                 getImage(ImageHandle image);
    [[nodiscard]] Buffer&                getBuffer(BufferHandle buffer);
    [[nodiscard]] const ImageCreateInfo& imageInfo(ImageHandle image);
    [[nodiscard]] vk::Extent2D imageExtent(ImageHandle image);
    [[nodiscard]] bool sharesImage(ImageHandle lhs, ImageHandle rhs) const;
    [[nodiscard]] vk::ImageUsageFlags imageUsage(ImageHandle image) const;
    void invalidateImageHistory(ImageHandle image);
    [[nodiscard]] uint64_t imageHistoryRevision(ImageHandle image) const;
    [[nodiscard]] vk::PipelineStageFlags2 firstUseStages(ImageHandle image) const;

    void beginGpuProfiling(
        vk::raii::CommandBuffer& commandBuffer, GpuProfiler& gpuProfiler, Profiler* profiler
    ) const;
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
        uint64_t                    historyRevision = 0;
        vk::BufferUsageFlags        bufferUsage;
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
        std::string                       name;
        bool                              retained = false;
        std::vector<ResourceHandle>       dependency;
        std::vector<ResourceHandle>       writeSrc;
        std::vector<PassNode*>            prev;
        std::vector<PassNode*>            next;
        std::vector<ResourceUse>          uses;
        std::function<void(RenderGraph&, const PassSetup&)> bindResources;
        std::function<void(RenderGraph&, const PassFrame&)> prepare;
        std::function<void(RenderGraph&, vk::raii::CommandBuffer&, const PassFrame&)> execute;
    };

    VulkanContext*                       deviceContext   = nullptr;
    std::vector<std::unique_ptr<PassNode>> nodeStorage;
    std::vector<PassNode*>                nodes;
    std::vector<PassNode*>                sortedNodes;
    PassNode*                            setupNode       = nullptr;
    std::vector<Resource>                resources;
    std::vector<ResourceHandle>          outputResources;
    std::array<StagingPool, maxFramesInFlight> stagingPools;
    uint32_t currentFrameIndex = 0;

    void finishStage(PassNode& node, PassNode* previousNode);

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

template <class Stage, class... Args>
auto RenderGraph::addStage(std::string_view name, Stage& stage, const PassSetup& setup, Args&&... args)
{
    auto& node   = *nodeStorage.emplace_back(std::make_unique<PassNode>());
    node.name    = name;
    node.execute = [pass = &stage](RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& frame)
    {
        pass->execute(graph, commandBuffer, frame);
    };
    if constexpr (requires { stage.bindResources(*this, setup); })
        node.bindResources = [pass = &stage](RenderGraph& graph, const PassSetup& setup)
        { pass->bindResources(graph, setup); };
    if constexpr (requires(const PassFrame& frame) { stage.prepare(*this, frame); })
        node.prepare = [pass = &stage](RenderGraph& graph, const PassFrame& frame)
        { pass->prepare(graph, frame); };
    nodes.push_back(&node);

    auto* previousNode = setupNode;
    setupNode          = &node;
    if constexpr (std::is_void_v<decltype(stage.compile(*this, setup, std::forward<Args>(args)...))>)
    {
        stage.compile(*this, setup, std::forward<Args>(args)...);
        finishStage(node, previousNode);
    }
    else
    {
        auto output = stage.compile(*this, setup, std::forward<Args>(args)...);
        finishStage(node, previousNode);
        return output;
    }
}
