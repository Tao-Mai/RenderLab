#pragma once

#include "core/Debug.h"
#include "core/Object.h"
#include "render/resources/Buffer.h"
#include "render/resources/Formats.h"
#include "render/resources/Resource.h"
#include "render/resources/ShaderManager.h"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class VulkanContext;

// TODO 之后优化相关管理
struct DescriptorSetLayout
{
    vk::raii::DescriptorSetLayout               handle = nullptr;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

    [[nodiscard]] vk::DescriptorSetLayout operator*() const { return *handle; }
    explicit                              operator bool() const { return static_cast<bool>(*handle); }
};

enum class VertexLayoutPreset
{
    Mesh,
    PositionOnly,
    PositionUv,
    None
};

struct PipelineState
{
    VertexLayoutPreset      vertexLayout = VertexLayoutPreset::Mesh;
    vk::PrimitiveTopology   topology     = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode         polygonMode  = vk::PolygonMode::eFill;
    vk::CullModeFlags       cullMode     = vk::CullModeFlagBits::eNone;
    vk::FrontFace           frontFace    = vk::FrontFace::eCounterClockwise;
    vk::SampleCountFlagBits samples      = vk::SampleCountFlagBits::e1;
    vk::CompareOp           depthCompare = vk::CompareOp::eLess;
    bool                    depthClamp   = false;
    bool                    depthTest    = true;
    bool                    depthWrite   = true;
    bool                    blend        = false;

    [[nodiscard]] bool operator==(const PipelineState&) const = default;
};

struct GraphicsPipelineInfo
{
    ShaderHandle            shader;
    vk::PipelineLayout      layout;
    PipelineState           state;
    std::vector<vk::Format> colorFormats;
    vk::Format              depthFormat = vk::Format::eUndefined;
    uint32_t                viewMask    = 0;
};

struct ComputePipeline
{
    vk::raii::Pipeline handle = nullptr;
    vk::Extent3D       threadGroupSize;

    [[nodiscard]] vk::Pipeline operator*() const { return *handle; }
    [[nodiscard]] vk::Extent3D groupCount(vk::Extent3D threads) const;
    void                       dispatch(vk::raii::CommandBuffer& commands, vk::Extent3D threads) const;
};

namespace Falcor
{
class RenderContext;

class FALCOR_API Device : public Object
{
    FALCOR_OBJECT(Device)

public:
    Device();
    ~Device() override;
    void                            init(VulkanContext& vulkan, ShaderManager& shaders);
    void                            shutdown() noexcept;
    void                            wait();
    [[nodiscard]] RenderContext*    getRenderContext() const { return mpRenderContext.get(); }
    [[nodiscard]] Falcor::ResourceBindFlags getFormatBindFlags(Falcor::ResourceFormat format) const;

    [[nodiscard]] ref<Buffer> createBuffer(
        size_t size,
        ResourceBindFlags bindFlags = ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        MemoryType memoryType = MemoryType::DeviceLocal,
        const void* initData = nullptr
    );
    [[nodiscard]] ref<Texture> createTexture1D(
        uint32_t width, Falcor::ResourceFormat format, uint32_t arraySize, uint32_t mipLevels, Falcor::ResourceBindFlags bindFlags
    ) const;
    [[nodiscard]] ref<Texture> createTexture2D(
        uint32_t          width,
        uint32_t          height,
        Falcor::ResourceFormat    format,
        uint32_t          arraySize,
        uint32_t          mipLevels,
        Falcor::ResourceBindFlags bindFlags
    ) const;
    [[nodiscard]] ref<Texture> createTexture2DMS(
        uint32_t          width,
        uint32_t          height,
        Falcor::ResourceFormat    format,
        uint32_t          sampleCount,
        uint32_t          arraySize,
        Falcor::ResourceBindFlags bindFlags
    ) const;
    [[nodiscard]] ref<Texture> createTexture3D(
        uint32_t          width,
        uint32_t          height,
        uint32_t          depth,
        Falcor::ResourceFormat    format,
        uint32_t          mipLevels,
        Falcor::ResourceBindFlags bindFlags
    ) const;
    [[nodiscard]] ref<Texture> createTextureCube(
        uint32_t          width,
        uint32_t          height,
        Falcor::ResourceFormat    format,
        uint32_t          arraySize,
        uint32_t          mipLevels,
        Falcor::ResourceBindFlags bindFlags
    ) const;

    [[nodiscard]] DescriptorSetLayout
    createDescriptorSetLayout(std::span<const vk::DescriptorSetLayoutBinding> bindings) const;
    [[nodiscard]] vk::PipelineLayout getOrCreatePipelineLayout(
        std::span<const DescriptorSetLayout* const> sets, std::span<const vk::PushConstantRange> ranges
    );
    [[nodiscard]] vk::raii::DescriptorSet allocateDescriptorSet(const DescriptorSetLayout& layout) const;
    [[nodiscard]] vk::Pipeline            getOrCreatePipeline(const GraphicsPipelineInfo& info);
    [[nodiscard]] ComputePipeline         createComputePipeline(ShaderHandle shader, vk::PipelineLayout layout) const;

    DescriptorSetLayout sceneDescriptorSetLayout;
    DescriptorSetLayout lightDescriptorSetLayout;
    DescriptorSetLayout shadowDescriptorSetLayout;
    DescriptorSetLayout materialDescriptorSetLayout;
    DescriptorSetLayout editorViewDescriptorSetLayout;
    vk::PipelineLayout  meshPipelineLayout;
    vk::PipelineLayout  editorPipelineLayout;

private:
    friend class Buffer;
    friend class Resource;
    friend class Texture;

    [[nodiscard]] ref<Texture> createTexture(
        Resource::Type    type,
        Falcor::ResourceFormat    format,
        vk::Extent3D      extent,
        uint32_t          arraySize,
        uint32_t          mipLevels,
        uint32_t          samples,
        Falcor::ResourceBindFlags bindFlags
    ) const;
    void createSharedLayouts();
    void createSharedPipelineLayouts();
    void createSharedDescriptorSetLayouts();

    struct PipelineLayoutKey
    {
        std::vector<std::vector<vk::DescriptorSetLayoutBinding>> sets;
        std::vector<vk::PushConstantRange>                       ranges;

        [[nodiscard]] bool operator==(const PipelineLayoutKey&) const = default;
    };
    struct PipelineLayoutKeyHash
    {
        [[nodiscard]] size_t operator()(const PipelineLayoutKey& key) const noexcept;
    };

    struct CachedPipelineLayout
    {
        std::vector<vk::raii::DescriptorSetLayout> setLayouts;
        vk::raii::PipelineLayout                   handle = nullptr;
    };

    struct PipelineKey
    {
        ShaderHandle            shader;
        vk::PipelineLayout      layout;
        PipelineState           state;
        std::vector<vk::Format> colorFormats;
        vk::Format              depthFormat;
        uint32_t                viewMask;

        [[nodiscard]] bool operator==(const PipelineKey&) const = default;
    };
    struct PipelineKeyHash
    {
        [[nodiscard]] size_t operator()(const PipelineKey& key) const noexcept;
    };

    DEBUG_ONLY(bool initialized = false;)
    std::unique_ptr<RenderContext> mpRenderContext;
    VulkanContext*                                                                     vulkan         = nullptr;
    const vk::raii::Device*                                                            device         = nullptr;
    ShaderManager*                                                                     shaders        = nullptr;
    vk::raii::DescriptorPool                                                           descriptorPool = nullptr;
    vk::raii::PipelineCache                                                            pipelineCache  = nullptr;
    std::unordered_map<PipelineKey, vk::raii::Pipeline, PipelineKeyHash>               pipelines;
    std::unordered_map<PipelineLayoutKey, CachedPipelineLayout, PipelineLayoutKeyHash> pipelineLayouts;
};
}  // namespace Falcor
