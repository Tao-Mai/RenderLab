#pragma once

#include "render/ShaderManager.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class DescriptorManager;

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
    bool                    depthTest    = true;
    bool                    depthWrite   = true;
    bool                    blend        = false;

    [[nodiscard]] bool operator==(const PipelineState&) const = default;
};

struct PipelineKey
{
    ShaderHandle            shader;
    vk::PipelineLayout      layout;
    PipelineState           state;
    std::vector<vk::Format> colorFormats;
    vk::Format              depthFormat = vk::Format::eUndefined;
    uint32_t                viewMask    = 0;

    [[nodiscard]] bool operator==(const PipelineKey&) const = default;
};

struct PipelineKeyHash
{
    [[nodiscard]] std::size_t operator()(const PipelineKey& key) const noexcept;
};

class PipelineManager
{
public:
    void init(const vk::raii::Device& device, DescriptorManager& descriptors, ShaderManager& shaders);
    void shutdown() noexcept;

    [[nodiscard]] vk::PipelineLayout
    getOrCreateLayout(std::span<const vk::DescriptorSetLayout> sets, std::span<const vk::PushConstantRange> ranges);
    [[nodiscard]] vk::Pipeline getOrCreate(const PipelineKey& key);

private:
    struct Layout
    {
        std::vector<vk::DescriptorSetLayout> sets;
        std::vector<vk::PushConstantRange>   ranges;
        vk::raii::PipelineLayout             layout;
    };
    std::vector<Layout>                                                  layouts;
    const vk::raii::Device*                                              device        = nullptr;
    DescriptorManager*                                                   descriptors   = nullptr;
    ShaderManager*                                                       shaders       = nullptr;
    vk::raii::PipelineCache                                              pipelineCache = nullptr;
    std::unordered_map<PipelineKey, vk::raii::Pipeline, PipelineKeyHash> pipelines;
};
