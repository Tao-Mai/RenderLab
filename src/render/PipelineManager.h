#pragma once

#include "render/ShaderManager.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include <vulkan/vulkan_raii.hpp>

class DescriptorManager;

enum class RenderMode
{
    Opaque, AlphaTest, Transparent, Shadow, DepthOnly, Skybox,
    PostProcess, LightMarker, Picking,
};

enum class PipelineLayoutPreset { SceneMaterial, SceneOnly };
enum class VertexLayoutPreset { Mesh, PositionOnly, None };

struct PipelineState
{
    VertexLayoutPreset vertexLayout = VertexLayoutPreset::Mesh;
    vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
    vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
    vk::CullModeFlags cullMode = vk::CullModeFlagBits::eNone;
    vk::FrontFace frontFace = vk::FrontFace::eClockwise;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    vk::CompareOp depthCompare = vk::CompareOp::eLess;
    bool depthTest = true;
    bool depthWrite = true;
    bool blend = false;

    [[nodiscard]] bool operator==(const PipelineState&) const = default;
    [[nodiscard]] static PipelineState preset(RenderMode mode);
};

struct PipelineKey
{
    ShaderHandle shader;
    PipelineLayoutPreset layout = PipelineLayoutPreset::SceneMaterial;
    PipelineState state;
    vk::Format colorFormat = vk::Format::eUndefined;
    vk::Format depthFormat = vk::Format::eUndefined;

    [[nodiscard]] bool operator==(const PipelineKey&) const = default;
};

struct PipelineKeyHash
{
    [[nodiscard]] std::size_t operator()(const PipelineKey& key) const noexcept;
};

class PipelineManager
{
public:
    void init(const vk::raii::Device& device, DescriptorManager& descriptors,
        ShaderManager& shaders);
    void reset() noexcept;

    [[nodiscard]] vk::PipelineLayout layout(PipelineLayoutPreset preset) const;
    [[nodiscard]] vk::Pipeline getOrCreate(const PipelineKey& key);

private:
    const vk::raii::Device* device = nullptr;
    DescriptorManager* descriptors = nullptr;
    ShaderManager* shaders = nullptr;
    vk::raii::PipelineLayout sceneMaterialLayout = nullptr;
    vk::raii::PipelineLayout sceneOnlyLayout = nullptr;
    vk::raii::PipelineCache pipelineCache = nullptr;
    std::unordered_map<PipelineKey, vk::raii::Pipeline, PipelineKeyHash> pipelines;
};
