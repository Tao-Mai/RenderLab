#include "render/resource/render_resource_manager.h"

#include "asset/asset_manager.h"
#include "core/context.h"
#include "render/device/frame_context.h"
#include "render/device/vulkan_context.h"
#include "render/resource/material.h"
#include "render/resource/mesh.h"
#include "render/resource/shader.h"
#include "render/resource/texture.h"

#include <utility>
#include <vector>
#include "logger.h"

RenderResourceManager::~RenderResourceManager() = default;

void RenderResourceManager::init(VulkanContext& targetVulkan, FrameContext& targetFrame)
{
    CHECK(context().assets != nullptr, "AssetManager must exist before RenderResourceManager");
    assets = context().assets;
    vulkan = &targetVulkan;
    frame = &targetFrame;
}

void RenderResourceManager::configureMaterialDescriptors(
    vk::DescriptorPool pool, vk::DescriptorSetLayout layout)
{
    descriptorPool = pool;
    materialLayout = layout;
}

void RenderResourceManager::reset() noexcept
{
    meshes.clear();
    materials.clear();
    textures.clear();
    shaders.clear();
    descriptorPool = nullptr;
    materialLayout = nullptr;
    frame = nullptr;
    vulkan = nullptr;
    assets = nullptr;
}

Mesh& RenderResourceManager::mesh(const AssetId& id)
{
    if (const auto found = meshes.find(id); found != meshes.end())
    {
        return *found->second;
    }
    MeshGeometry geometry = assets->loadGeometry(id);
    const MeshDesc& desc = assets->meshDesc(id);
    std::vector<Submesh> parts;
    parts.reserve(desc.submeshes.size());
    for (const SubmeshDesc& submesh : desc.submeshes)
    {
        parts.push_back({
            .firstIndex = submesh.firstIndex,
            .indexCount = submesh.indexCount,
            .material = &material(submesh.materialId),
        });
    }
    auto gpu = std::make_unique<Mesh>(
        frame->uploadContext(*vulkan), std::move(geometry), std::move(parts));
    return *meshes.emplace(id, std::move(gpu)).first->second;
}

Material& RenderResourceManager::material(const AssetId& id)
{
    if (const auto found = materials.find(id); found != materials.end())
    {
        return *found->second;
    }
    CHECK(descriptorPool && materialLayout, "material descriptor layout is not configured");
    const MaterialDesc& desc = assets->materialDesc(id);
    std::shared_ptr<Texture> baseColor = texture(desc.baseColorTexture);
    auto gpu = std::make_unique<Material>();
    gpu->create(
        vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
        descriptorPool, materialLayout, desc, std::move(baseColor));
    return *materials.emplace(id, std::move(gpu)).first->second;
}

std::shared_ptr<Texture> RenderResourceManager::texture(const AssetId& id)
{
    if (const auto found = textures.find(id); found != textures.end())
    {
        return found->second;
    }
    const TextureDesc& desc = assets->textureDesc(id);
    std::shared_ptr<Texture> gpu;
    if (desc.source == "solid")
    {
        gpu = std::make_shared<Texture>(frame->uploadContext(*vulkan), *desc.rgba);
    }
    else
    {
        gpu = std::make_shared<Texture>(
            frame->uploadContext(*vulkan), assets->path(*desc.path));
    }
    return textures.emplace(id, std::move(gpu)).first->second;
}

Shader& RenderResourceManager::shader(const AssetId& id)
{
    if (const auto found = shaders.find(id); found != shaders.end())
    {
        return *found->second;
    }
    const ShaderDesc& desc = assets->shaderDesc(id);
    auto gpu = std::make_unique<Shader>(
        vulkan->deviceHandle(), desc.binary.string());
    return *shaders.emplace(id, std::move(gpu)).first->second;
}
