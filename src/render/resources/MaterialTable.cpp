#include "render/resources/MaterialTable.h"

#include "render/RhiUtils.h"

#include <slang-rhi.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/StandardMaterial.h"
#include "core/Logger.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/SceneLightConstants.h"

#include <cstddef>
#include <span>
#include <utility>

namespace
{
[[nodiscard]] NormalEncoding normalEncoding(const std::optional<TextureBinding>& binding, const AssetManager& assets)
{
    if (binding)
    {
        const ImageFormat format = assets.get<TextureAsset>(binding->textureID).format;
        if (format == ImageFormat::BC5Unorm)
        {
            return NormalEncoding::RgUnorm;
        }
        if (format == ImageFormat::BC5Snorm)
        {
            return NormalEncoding::RgSnorm;
        }
    }
    return NormalEncoding::Rgb;
}

}  // namespace


void MaterialTable::init(rhi::IDevice& targetDevice, RenderResourceManager& targetResources)
{
    device = &targetDevice;
    resources = &targetResources;
    reset();
}

void MaterialTable::reset()
{
    materials.clear();
    materialInfos.clear();
    materialIndices.clear();
    textureIndices.clear();
    pendingImages.clear();
    samplerIndices.clear();
    images = {};
    materialBindings.clear();
    ++materialRevision;
    ++emissiveRevision;

    static_cast<void>(textureReference({BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat}));
    samplers.fill(samplers[0]);

    // Earlier submissions retain their material buffer and immutable parameter-block bindings.
    rhiCheck(device->createBuffer(
        {.size  = sizeof(StandardMaterialData),
         .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr,
        materialBuffer.writeRef()
    ));
    uploadedMaterialRevision.reset();
}

TextureRef MaterialTable::textureReference(const TextureBinding& binding)
{
    const auto [image, newImage] =
        textureIndices.try_emplace(binding.textureID, static_cast<uint32_t>(textureIndices.size()));
    if (newImage)
    {
        CHECK(
            image->second < kMaxMaterialTextures,
            "material texture '{}' exceeds capacity {}",
            binding.textureID,
            kMaxMaterialTextures
        );
        pendingImages.push_back(binding.textureID);
    }

    const auto [sampler, newSampler] =
        samplerIndices.try_emplace(binding.samplerID, static_cast<uint32_t>(samplerIndices.size()));
    if (newSampler)
    {
        CHECK(
            sampler->second < kMaxMaterialSamplers,
            "material sampler '{}' exceeds capacity {}",
            binding.samplerID,
            kMaxMaterialSamplers
        );
        samplers[sampler->second] = resources->sampler(binding.samplerID);
    }
    if (newSampler)
        materialBindings.clear();

    return {.textureIndex = image->second, .samplerIndex = sampler->second};
}

uint32_t MaterialTable::resolve(const AssetID<StandardMaterial>& id, const AssetManager& assets)
{
    const auto& material = assets.get(id);
    StandardMaterialData data{
        .baseColor            = material.baseColor,
        .specularParams       = material.specularParams,
        .emissiveColor        = material.emissiveColor,
        .emissiveFactor       = material.emissiveFactor,
        .transmissionColor    = material.transmissionColor,
        .diffuseTransmission  = material.diffuseTransmission,
        .specularTransmission = material.specularTransmission,
        .indexOfRefraction    = material.indexOfRefraction,
        .normalScale          = material.normalScale,
        .alphaCutoff          = material.alphaCutoff,
        .shadingModel         = static_cast<uint32_t>(material.shadingModel),
        .alphaMode            = static_cast<uint32_t>(material.alphaMode),
        .doubleSided =
            material.doubleSided || material.diffuseTransmission > 0.0f || material.specularTransmission > 0.0f,
        .thinSurface    = material.thinSurface,
        .normalEncoding = normalEncoding(material.normalTexture, assets),
    };
    const auto resolveTexture = [&](const std::optional<TextureBinding>& source)
    { return source ? textureReference(*source) : TextureRef{}; };
    data.baseColorTexture    = resolveTexture(material.baseColorTexture);
    data.specularTexture     = resolveTexture(material.specularTexture);
    data.emissiveTexture     = resolveTexture(material.emissiveTexture);
    data.normalTexture       = resolveTexture(material.normalTexture);
    data.transmissionTexture = resolveTexture(material.transmissionTexture);

    return store(
        id,
        data,
        {
            .emissive = material.emissiveFactor > 0.0f &&
                (material.emissiveTexture.has_value() ||
                 glm::any(glm::greaterThan(material.emissiveColor, glm::vec3{0.0f}))),
        }
    );
}

uint32_t MaterialTable::store(const AssetID<StandardMaterial>& id, const StandardMaterialData& data, MaterialInfo info)
{
    const auto [found, added] = materialIndices.try_emplace(id, static_cast<uint32_t>(materials.size()));
    if (added)
    {
        materials.push_back(data);
        materialInfos.push_back(info);
        ++materialRevision;
        if (info.emissive)
            ++emissiveRevision;
    }
    else
    {
        auto& previous = materials[found->second];
        auto& previousInfo = materialInfos[found->second];
        if (previousInfo.emissive != info.emissive ||
            (info.emissive &&
             (previous.emissiveColor != data.emissiveColor || previous.emissiveFactor != data.emissiveFactor ||
              previous.emissiveTexture != data.emissiveTexture)))
        {
            ++emissiveRevision;
        }
        if (previous != data)
        {
            previous = data;
            ++materialRevision;
        }
        previousInfo = info;
    }
    return found->second;
}

bool MaterialTable::upload(rhi::ICommandEncoder& commands)
{
    const bool imagesChanged = !pendingImages.empty();
    if (imagesChanged)
    {
        for (const auto& id : pendingImages)
        {
            const auto index = textureIndices.at(id);
            auto view = resources->texture(id, commands)->getDefaultView();
            if (index == 0)
                images.fill(view);
            else
                images[index] = std::move(view);
        }
        pendingImages.clear();
        materialBindings.clear();
    }

    const bool materialsChanged = uploadedMaterialRevision != materialRevision;
    if (materialsChanged)
    {
        const std::array<std::byte, sizeof(StandardMaterialData)> empty{};
        const auto bytes = materials.empty() ? std::span<const std::byte>{empty} : std::as_bytes(std::span{materials});
        auto* previousBuffer = materialBuffer.get();
        ensureBufferCapacity(*device, materialBuffer, bytes.size());
        if (materialBuffer.get() != previousBuffer)
            materialBindings.clear();
        rhiCheck(commands.uploadBufferData(materialBuffer, 0, bytes.size(), bytes.data()));
        uploadedMaterialRevision = materialRevision;
    }

    return materialsChanged || imagesChanged;
}

void MaterialTable::bind(rhi::IShaderObject& rootObject, const rhi::ShaderCursor& cursor) const
{
    auto* layout = cursor.getTypeLayout()->getElementTypeLayout();
    auto& binding = materialBindings[layout];
    if (!binding)
    {
        rhiCheck(
            device->createShaderObjectFromTypeLayout(rootObject.getSlangComponentType(), layout, binding.writeRef())
        );
        const rhi::ShaderCursor table(binding);
        rhiCheck(table["materialData"].setBinding(materialBuffer));
        for (uint32_t index = 0; index < images.size(); ++index)
            rhiCheck(table["materialTextures"][index].setBinding(images[index]));
        for (uint32_t index = 0; index < samplers.size(); ++index)
            rhiCheck(table["materialSamplers"][index].setBinding(samplers[index]));
        rhiCheck(binding->finalize());
    }
    rhiCheck(cursor.setObject(binding));
}
