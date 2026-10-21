#pragma once

#include "core/Debug.h"
#include "render/ShaderManager.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

struct DescriptorSetLayout
{
    vk::raii::DescriptorSetLayout               handle = nullptr;
    std::vector<vk::DescriptorSetLayoutBinding> bindings;

    [[nodiscard]] vk::DescriptorSetLayout operator*() const { return *handle; }
    explicit                              operator bool() const { return static_cast<bool>(*handle); }
};

struct PipelineLayout
{
    vk::raii::PipelineLayout             handle = nullptr;
    std::vector<ShaderMetadata::Binding> bindings;
    std::vector<vk::PushConstantRange>   ranges;
    uint64_t                             id = 0;

    [[nodiscard]] vk::PipelineLayout operator*() const { return *handle; }
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
    const PipelineLayout*   layout = nullptr;
    PipelineState           state;
    std::vector<vk::Format> colorFormats;
    vk::Format              depthFormat = vk::Format::eUndefined;
    uint32_t                viewMask    = 0;
};

class Device
{
public:
    ~Device();
    void init(const vk::raii::Device& device, ShaderManager& shaders);
    void shutdown() noexcept;

    [[nodiscard]] DescriptorSetLayout
    createDescriptorSetLayout(std::span<const vk::DescriptorSetLayoutBinding> bindings) const;
    [[nodiscard]] PipelineLayout createPipelineLayout(
        std::span<const DescriptorSetLayout* const> sets, std::span<const vk::PushConstantRange> ranges
    );
    [[nodiscard]] vk::raii::DescriptorSet allocateDescriptorSet(const DescriptorSetLayout& layout) const;
    [[nodiscard]] vk::Pipeline            getOrCreatePipeline(const GraphicsPipelineInfo& info);

    DescriptorSetLayout sceneDescriptorSetLayout;
    DescriptorSetLayout materialDescriptorSetLayout;
    DescriptorSetLayout editorViewDescriptorSetLayout;
    PipelineLayout      meshPipelineLayout;
    PipelineLayout      editorPipelineLayout;

private:
    void createSharedLayouts();
    void createSharedPipelineLayouts();
    void createSharedDescriptorSetLayouts();

    struct PipelineKey
    {
        ShaderHandle            shader;
        uint64_t                layoutId;
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
    const vk::raii::Device*                                              device         = nullptr;
    ShaderManager*                                                       shaders        = nullptr;
    vk::raii::DescriptorPool                                             descriptorPool = nullptr;
    vk::raii::PipelineCache                                              pipelineCache  = nullptr;
    std::unordered_map<PipelineKey, vk::raii::Pipeline, PipelineKeyHash> pipelines;
    uint64_t                                                             nextLayoutId = 1;
};
