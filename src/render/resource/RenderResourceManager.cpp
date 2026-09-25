#include "render/resource/RenderResourceManager.h"

#include "asset/AssetDescManager.h"
#include "asset/AssetDataManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/DescriptorManager.h"
#include "render/ShaderManager.h"
#include "render/device/VulkanContext.h"
#include "render/device/VkCheck.h"
#include "render/resource/Material.h"
#include "render/resource/Mesh.h"
#include "render/resource/Texture.h"

#include <span>
#include <utility>
#include <vector>

#include <rfl/json.hpp>

RenderResourceManager::~RenderResourceManager() = default;

GpuUploadContext RenderResourceManager::uploadContext() const
{
    CHECK(vulkan != nullptr && *uploadPool,
          "RenderResourceManager upload context is not initialized");
    return {vulkan->physicalDeviceHandle(), vulkan->deviceHandle(),
            uploadPool, vulkan->queueHandle()};
}

void RenderResourceManager::init(VulkanContext&     targetVulkan,
                                 DescriptorManager& targetDescriptors, ShaderManager& targetShaders)
{
    CHECK(context().assetManager != nullptr,
          "AssetDescManager must exist before RenderResourceManager");
    CHECK(context().assetDataManager != nullptr,
          "AssetDataManager must exist before RenderResourceManager");
    assets      = context().assetManager;
    data        = context().assetDataManager;
    vulkan      = &targetVulkan;
    descriptors = &targetDescriptors;
    shaders     = &targetShaders;
    const vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = vulkan->graphicsQueueFamilyIndex(),
    };
    uploadPool = vkCheck(vulkan->deviceHandle().createCommandPool(poolInfo));
}

void RenderResourceManager::reset() noexcept
{
    meshes.clear();
    materials.clear();
    textures.clear();
    uploadPool  = nullptr;
    descriptors = nullptr;
    shaders     = nullptr;
    vulkan      = nullptr;
    assets      = nullptr;
    data        = nullptr;
}

Mesh& RenderResourceManager::mesh(const AssetId& id)
{
    if (const auto found = meshes.find(id); found != meshes.end())
    {
        return *found->second;
    }

    const MeshDesc& meshDesc = assets->desc<MeshDesc>(id);
    CHECK(!meshDesc.geometry.empty(),
          "mesh {} has no imported geometry; import its source first",
          id);

    MeshGeometry         geometry = data->readGeometry(meshDesc.geometry);
    std::vector<Submesh> parts;
    parts.reserve(meshDesc.submeshes.size());
    for (const SubmeshDesc& submesh : meshDesc.submeshes)
    {
        parts.push_back({
            .firstIndex = submesh.firstIndex,
            .indexCount = submesh.indexCount,
            .material = &material(submesh.materialId),
        });
    }

    auto gpu = std::make_unique<Mesh>(
        uploadContext(),
        std::move(geometry),
        std::move(parts));
    return *meshes.emplace(id, std::move(gpu)).first->second;
}

MaterialDesc RenderResourceManager::materialDesc(const AssetId& id) const
{
    CHECK(assets != nullptr, "RenderResourceManager is not initialized");
    return assets->desc<MaterialDesc>(id);
}

Material& RenderResourceManager::material(const AssetId& id)
{
    return material(materialDesc(id));
}

Material& RenderResourceManager::material(const MaterialDesc& desc)
{
    CHECK(descriptors != nullptr, "RenderResourceManager is not initialized");

    const std::string key = rfl::json::write(desc);
    if (const auto found = materials.find(key); found != materials.end())
    {
        return *found->second;
    }

    const AssetId            albedoId  = desc.baseColorTexture.value_or(TextureDesc::white);
    std::shared_ptr<Texture> baseColor =
        texture(albedoId.empty() ? TextureDesc::white : albedoId);
    auto gpu = std::make_unique<Material>();
    gpu->create(
        vulkan->physicalDeviceHandle(),
        vulkan->deviceHandle(),
        *descriptors,
        shaders->getOrLoad(desc.shaderId.value_or("scene")),
        desc,
        std::move(baseColor));
    return *materials.emplace(key, std::move(gpu)).first->second;
}

std::shared_ptr<Texture> RenderResourceManager::texture(const AssetId& id)
{
    if (const auto found = textures.find(id); found != textures.end())
    {
        return found->second;
    }

    const TextureDesc& desc = assets->desc<TextureDesc>(id);
    std::shared_ptr<Texture> gpu;
    if (desc.source == "solid")
    {
        CHECK(desc.format == ImageFormat::RGBA8,
              "solid texture '{}' must use RGBA8",
              id);
        CHECK(desc.layout == ImageLayout::Image2D,
              "solid texture '{}' must be 2D",
              id);
        CHECK(desc.width == 1 && desc.height == 1,
              "solid texture '{}' must be 1x1",
              id);
        CHECK(desc.rgba.has_value(), "solid texture '{}' has no RGBA value", id);
        gpu = std::make_shared<Texture>(
            uploadContext(),
            desc,
            std::span<const uint8_t>{*desc.rgba});
    }
    else if (desc.source == "file" || desc.source == "environment")
    {
        CHECK((desc.source == "file" &&
                  desc.layout == ImageLayout::Image2D) ||
              (desc.source == "environment" &&
                  desc.layout == ImageLayout::Cubemap &&
                  (desc.format == ImageFormat::RGBA8 ||
                      desc.format == ImageFormat::RGBA16F ||
                      desc.format == ImageFormat::RGBA32F)),
              "texture '{}' source, layout and data format disagree",
              id);
        const std::vector<uint8_t> bytes = data->readTexture(desc);
        gpu = std::make_shared<Texture>(uploadContext(), desc, bytes);
    }
    else if (desc.source == kBuiltinTextureSource)
    {
        const std::vector<uint8_t> bytes = data->readTexture(desc);
        gpu = std::make_shared<Texture>(uploadContext(), desc, bytes);
    }
    else
    {
        CHECK(false, "unknown texture source '{}' for '{}'", desc.source, id);
    }

    return textures.emplace(id, std::move(gpu)).first->second;
}
