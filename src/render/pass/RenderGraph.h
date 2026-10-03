#pragma once

#include "asset/Asset.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class Buffer;
class DescriptorManager;
class FrameContext;
class GpuScene;
class GpuTexture;
class PipelineManager;
class Renderer;
class RenderResourceManager;
class ShaderManager;
class Swapchain;
class VulkanContext;
struct EditorFrameInput;

class RenderGraph
{
public:
    using ResourceHandle = uint32_t;
    static constexpr ResourceHandle invalidHandle = std::numeric_limits<ResourceHandle>::max();
    enum class ResourceType { Image, Buffer };

    // Creation fields describe the physical resource. Usage/stages/access/layout
    // describe this slot's use of the whole resource; image subranges are not split.
    struct ResourceDesc
    {
        ResourceType type = ResourceType::Image;
        vk::Format format = vk::Format::eUndefined;
        vk::Extent3D extent{0, 0, 1};
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        vk::ImageCreateFlags imageFlags;
        vk::ImageViewType viewType = vk::ImageViewType::e2D;
        vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
        vk::DeviceSize size = 0;
        vk::MemoryPropertyFlags memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        vk::ImageUsageFlags imageUsage;
        vk::BufferUsageFlags bufferUsage;
        vk::PipelineStageFlags2 stages;
        vk::AccessFlags2 access;
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    };

    struct ResourceState
    {
        vk::PipelineStageFlags2 stages;
        vk::AccessFlags2 access;
        vk::ImageLayout layout = vk::ImageLayout::eUndefined;
    };

    struct PassInfo
    {
        std::string name;
        bool enabled = true;
        std::vector<ResourceHandle> inputResourceHandles;
        std::vector<ResourceHandle> outputResourceHandles;
        std::function<void(RenderGraph&)> setupPass;
        std::function<void(RenderGraph&)> executePass;
    };

    // Imported handles are borrowed. One instance is shared, frame-count instances
    // use the frame index, and swapchain instances use the acquired image index.
    struct ExternalResource
    {
        vk::Image image;
        vk::ImageView view;
        Buffer* buffer = nullptr;
        ResourceState state;
    };

    void init();
    void reset() noexcept;
    void build();
    void bindSceneTextures(const SceneAsset& scene);
    void prepareRenderData(uint32_t frameIndex);
    void execute(const EditorFrameInput& editor,
        uint32_t frameIndex, uint32_t imageIndex);
    [[nodiscard]] uint32_t readSelectionId(uint32_t frameIndex);

    std::tuple<std::vector<ResourceHandle>, std::vector<ResourceHandle>> registerPass(
        std::string_view name, uint32_t inputSlotCount, uint32_t outputSlotCount,
        std::function<void(RenderGraph&)> setup = {},
        std::function<void(RenderGraph&)> execute = {});
    void setPassEnabled(std::string_view name, bool enabled);
    [[nodiscard]] bool passEnabled(std::string_view name) const;
    [[nodiscard]] ResourceHandle outputSlot(std::string_view name, uint32_t index) const;

    void createResource(ResourceHandle input, const ResourceDesc& desc);
    void importResource(ResourceHandle input, const ResourceDesc& desc,
        std::vector<ExternalResource> instances, bool swapchainIndexed = false);
    void bindInput(ResourceHandle input, ResourceHandle output, const ResourceDesc& desc);
    void bindOutput(ResourceHandle output, ResourceHandle input, const ResourceDesc& desc);
    void ignoreInput(ResourceHandle input);
    [[nodiscard]] std::vector<std::string> compile();
    [[nodiscard]] std::vector<std::string> executionOrder() const;
    [[nodiscard]] const ResourceDesc& resourceDesc(ResourceHandle input) const;
    [[nodiscard]] static bool needsBarrier(const ResourceState& previous, const ResourceDesc& next);

    [[nodiscard]] vk::Image image(ResourceHandle input) const;
    [[nodiscard]] vk::ImageView imageView(ResourceHandle input) const;
    [[nodiscard]] vk::ImageView imageView(ResourceHandle input, uint32_t frameIndex) const;
    [[nodiscard]] vk::ImageView layerView(ResourceHandle input, uint32_t layer) const;
    [[nodiscard]] Buffer& buffer(ResourceHandle input) const;
    [[nodiscard]] Buffer& buffer(ResourceHandle input, uint32_t frameIndex) const;
    // Apply an output state during execution when commands need it before pass end.
    void useOutput(ResourceHandle output);

    void importSwapchain(ResourceHandle input, const ResourceDesc& use);
    void importTexture(ResourceHandle input, const GpuTexture& texture);
    [[nodiscard]] VulkanContext& vulkan() const;
    [[nodiscard]] Swapchain& swapchain() const;
    [[nodiscard]] PipelineManager& pipelines() const;
    [[nodiscard]] ShaderManager& shaders() const;
    [[nodiscard]] RenderResourceManager& assetResources() const;
    [[nodiscard]] FrameContext& frameContext(uint32_t frameIndex) const;
    [[nodiscard]] const GpuScene& scene() const;
    [[nodiscard]] vk::Format depthFormat() const;
    [[nodiscard]] vk::Format shadowFormat() const;
    [[nodiscard]] vk::raii::CommandBuffer& commands() const;
    [[nodiscard]] uint32_t frameIndex() const;
    [[nodiscard]] const EditorFrameInput& editorInput() const;

private:
    struct Passes;
    struct Allocation;
    enum class Binding { Unbound, Resource, Alias, Ignored };
    struct Slot
    {
        PassInfo* owner = nullptr;
        Binding binding = Binding::Unbound;
        ResourceHandle ref = invalidHandle;
        ResourceHandle resource = invalidHandle;
        ResourceDesc desc;
    };
    struct Resource
    {
        ResourceDesc creation;
        ResourceDesc merged;
        bool external = false;
        bool swapchainIndexed = false;
        std::vector<ExternalResource> instances;
        std::shared_ptr<Allocation> allocation;
    };

    Renderer* renderer = nullptr;
    std::shared_ptr<Passes> passes;
    std::unordered_map<std::string, std::unique_ptr<PassInfo>> passInfos;
    std::vector<PassInfo*> registeredPasses;
    std::vector<PassInfo*> orderedPasses;
    std::vector<Slot> globalInputSlots;
    std::vector<Slot> globalOutputSlots;
    std::vector<Resource> resources;
    bool compiled = false;
    bool built = false;
    vk::Format sceneDepthFormat = vk::Format::eUndefined;
    vk::Format pointShadowFormat = vk::Format::eUndefined;
    uint32_t currentFrame = 0;
    uint32_t currentImage = 0;
    const EditorFrameInput* currentEditor = nullptr;
    PassInfo* executingPass = nullptr;

    void allocateResources();
    [[nodiscard]] ResourceHandle importExternal(const ResourceDesc& desc,
        std::vector<ExternalResource> instances, bool swapchainIndexed);
    void validateCompilation();
    void useResource(ResourceHandle resource, const ResourceDesc& use);
    [[nodiscard]] Allocation& allocation(ResourceHandle input) const;
    [[nodiscard]] uint32_t instanceIndex(const Resource& resource, uint32_t frameIndex) const;
};
