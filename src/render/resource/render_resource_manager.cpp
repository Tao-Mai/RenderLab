#include "render/resource/render_resource_manager.h"

#include "asset/asset_manager.h"
#include "asset/builtin_meshes.h"
#include "asset/geometry_io.h"
#include "asset/gltf_loader.h"
#include "core/context.h"
#include "core/logger.h"
#include "render/device/frame_context.h"
#include "render/device/vulkan_context.h"
#include "render/resource/material.h"
#include "render/resource/mesh.h"
#include "render/resource/shader.h"
#include "render/resource/texture.h"

#include <array>
#include <utility>
#include <vector>

#include <rfl/json.hpp>

namespace
{
[[nodiscard]] MeshGeometry builtinMeshGeometry(const AssetId& id)
{
    if (id == MeshDesc::cube)
    {
        return BuiltinMeshes::cube();
    }
    if (id == MeshDesc::sphere)
    {
        return BuiltinMeshes::sphere();
    }
    CHECK(id == MeshDesc::arrow, "unknown builtin mesh: {}", id);
    return BuiltinMeshes::arrow();
}

[[nodiscard]] MaterialDesc builtinMaterialDesc(const AssetId& id)
{
    CHECK(id == MaterialDesc::white, "unknown builtin material: {}", id);
    MaterialDesc desc{};
    desc.id = MaterialDesc::white;
    desc.metallic = 0.0f;
    desc.roughness = 0.4f;
    desc.baseColorTexture = TextureDesc::white;
    return desc;
}
}

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

    MeshGeometry geometry;
    std::vector<Submesh> parts;
    if (isBuiltin<MeshDesc>(id))
    {
        geometry = builtinMeshGeometry(id);
        parts.push_back({
            .firstIndex = 0,
            .indexCount = static_cast<uint32_t>(geometry.indices.size()),
            .material = &material(MaterialDesc::white),
        });
    }
    else
    {
        const MeshDesc& meshDesc = assets->desc<MeshDesc>(id);
        std::filesystem::path geometryPath = meshDesc.geometry.empty()
            ? std::filesystem::path{}
            : assets->path(meshDesc.geometry);
        if (geometryPath.empty() || !std::filesystem::exists(geometryPath))
        {
            CHECK(meshDesc.source.kind == "gltf" && meshDesc.source.path.has_value() &&
                  !meshDesc.source.path->empty(),
                "mesh {} has no geometry and no import source", id);
            const MeshSourceDesc source = meshDesc.source;
            GLTFLoader::import(*assets, id, assets->path(*source.path), source);
            geometryPath = assets->path(assets->desc<MeshDesc>(id).geometry);
        }
        geometry = geometry_io::read(geometryPath);
        const MeshDesc& desc = assets->desc<MeshDesc>(id);
        parts.reserve(desc.submeshes.size());
        for (const SubmeshDesc& submesh : desc.submeshes)
        {
            parts.push_back({
                .firstIndex = submesh.firstIndex,
                .indexCount = submesh.indexCount,
                .material = &material(submesh.materialId),
            });
        }
    }

    auto gpu = std::make_unique<Mesh>(
        frame->uploadContext(*vulkan), std::move(geometry), std::move(parts));
    return *meshes.emplace(id, std::move(gpu)).first->second;
}

MaterialDesc RenderResourceManager::materialDesc(const AssetId& id) const
{
    if (isBuiltin<MaterialDesc>(id))
    {
        return builtinMaterialDesc(id);
    }
    CHECK(assets != nullptr, "RenderResourceManager is not initialized");
    return assets->desc<MaterialDesc>(id);
}

Material& RenderResourceManager::material(const AssetId& id)
{
    return material(materialDesc(id));
}

Material& RenderResourceManager::material(const MaterialDesc& desc)
{
    CHECK(descriptorPool && materialLayout, "material descriptor layout is not configured");

    const std::string key = rfl::json::write(desc);
    if (const auto found = materials.find(key); found != materials.end())
    {
        return *found->second;
    }

    const AssetId albedoId = desc.baseColorTexture.value_or(TextureDesc::white);
    std::shared_ptr<Texture> baseColor =
        texture(albedoId.empty() ? TextureDesc::white : albedoId);
    auto gpu = std::make_unique<Material>();
    gpu->create(
        vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
        descriptorPool, materialLayout, desc, std::move(baseColor));
    return *materials.emplace(key, std::move(gpu)).first->second;
}

std::shared_ptr<Texture> RenderResourceManager::texture(const AssetId& id)
{
    if (const auto found = textures.find(id); found != textures.end())
    {
        return found->second;
    }

    std::shared_ptr<Texture> gpu;
    if (isBuiltin<TextureDesc>(id))
    {
        CHECK(id == TextureDesc::white, "unknown builtin texture: {}", id);
        constexpr std::array<uint8_t, 4> white{255, 255, 255, 255};
        gpu = std::make_shared<Texture>(frame->uploadContext(*vulkan), white);
    }
    else
    {
        const TextureDesc& desc = assets->desc<TextureDesc>(id);
        if (desc.source == "solid")
        {
            gpu = std::make_shared<Texture>(frame->uploadContext(*vulkan), *desc.rgba);
        }
        else
        {
            gpu = std::make_shared<Texture>(
                frame->uploadContext(*vulkan), assets->path(*desc.path));
        }
    }
    return textures.emplace(id, std::move(gpu)).first->second;
}

Shader& RenderResourceManager::shader(const AssetId& id)
{
    if (const auto found = shaders.find(id); found != shaders.end())
    {
        return *found->second;
    }
    const ShaderDesc& desc = assets->desc<ShaderDesc>(id);
    auto gpu = std::make_unique<Shader>(
        vulkan->deviceHandle(), desc.binary.string());
    return *shaders.emplace(id, std::move(gpu)).first->second;
}
