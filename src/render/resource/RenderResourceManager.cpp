#include "render/resource/RenderResourceManager.h"

#include "asset/AssetManager.h"
#include "asset/BuiltinMeshes.h"
#include "asset/GeometryIo.h"
#include "asset/GltfLoader.h"
#include "asset/TextureIo.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/device/FrameContext.h"
#include "render/device/VulkanContext.h"
#include "render/resource/Material.h"
#include "render/resource/Mesh.h"
#include "render/resource/Shader.h"
#include "render/resource/Texture.h"

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
    if (id == MeshDesc::plane)
    {
        return BuiltinMeshes::plane();
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
    CHECK(context().assetManager != nullptr, "AssetManager must exist before RenderResourceManager");
    assets = context().assetManager;
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
        if (id == TextureDesc::white)
        {
            constexpr std::array<uint8_t, 4> white{255, 255, 255, 255};
            gpu = std::make_shared<Texture>(frame->uploadContext(*vulkan), white);
        }
        else
        {
            CHECK(id == TextureDesc::whiteCube, "unknown builtin texture: {}", id);
            TextureDesc whiteCube{};
            whiteCube.format = TextureDesc::DataFormat::Rgba8Srgb;
            whiteCube.layout = TextureDesc::Layout::Cubemap;
            whiteCube.width = 1;
            whiteCube.height = 1;
            const std::vector<uint8_t> bytes(6 * 4, 255);
            gpu = std::make_shared<Texture>(
                frame->uploadContext(*vulkan), whiteCube, bytes);
        }
    }
    else
    {
        const TextureDesc& desc = assets->desc<TextureDesc>(id);
        if (desc.source == "solid")
        {
            CHECK(desc.format == TextureDesc::DataFormat::Rgba8Srgb,
                "solid texture '{}' must use Rgba8Srgb", id);
            CHECK(desc.layout == TextureDesc::Layout::Image2D,
                "solid texture '{}' must be 2D", id);
            CHECK(desc.rgba.has_value(), "solid texture '{}' has no RGBA value", id);
            gpu = std::make_shared<Texture>(frame->uploadContext(*vulkan), *desc.rgba);
        }
        else if (desc.source == "file" || desc.source == "environment")
        {
            CHECK((desc.source == "file" &&
                    desc.layout == TextureDesc::Layout::Image2D) ||
                    (desc.source == "environment" &&
                    desc.layout == TextureDesc::Layout::Cubemap &&
                    (desc.format == TextureDesc::DataFormat::Rgba16Float ||
                     desc.format == TextureDesc::DataFormat::Rgba32Float)),
                "texture '{}' source, layout and data format disagree", id);
            CHECK(desc.binary.has_value() && !desc.binary->empty(),
                "texture '{}' has no binary path", id);
            const std::vector<uint8_t> bytes = texture_io::read(
                assets->path(*desc.binary), desc);
            gpu = std::make_shared<Texture>(frame->uploadContext(*vulkan), desc, bytes);
        }
        else
        {
            CHECK(false, "unknown texture source '{}' for '{}'", desc.source, id);
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
