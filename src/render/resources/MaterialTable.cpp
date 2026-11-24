#include "render/resources/MaterialTable.h"

#include "render/device/BufferCapacity.h"

#include <slang-rhi.h>
#include "asset/ApplyOptionalFields.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/GltfMaterial.h"
#include "asset/pbrt/PbrtMaterial.h"
#include "core/Logger.h"
#include "render/DrawItem.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/SceneLightConstants.h"
#include "render/resources/ShaderData.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <unordered_set>
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
pbrt::TextureMapping textureMapping(PbrtTextureMapping::Kind kind)
{
    switch (kind)
    {
        case PbrtTextureMapping::Kind::Uv:
            return pbrt::TextureMapping::Uv;
        case PbrtTextureMapping::Kind::Spherical:
            return pbrt::TextureMapping::Spherical;
        case PbrtTextureMapping::Kind::Cylindrical:
            return pbrt::TextureMapping::Cylindrical;
        case PbrtTextureMapping::Kind::Planar:
            return pbrt::TextureMapping::Planar;
        case PbrtTextureMapping::Kind::Spatial:
            return pbrt::TextureMapping::Spatial;
    }
    std::unreachable();
}

class TextureCompiler
{
public:
    TextureCompiler(
        std::vector<pbrt::TextureInstruction>& output,
        const AssetManager&                    assets,
        const AssetDataManager&                assetData,
        MaterialTable&                         materialTable
    ) :
        output(output), assets(assets), assetData(assetData), materialTable(materialTable)
    {
    }

    template <class Input>
    uint32_t compile(const Input& source, PbrtTextureType type)
    {
        CHECK(output.size() < std::numeric_limits<uint32_t>::max());
        const auto begin = static_cast<uint32_t>(output.size());
        output.push_back(node(pbrt::TextureOp::Program));
        stackDepth = 0;
        input(source, type);

        CHECK(output.size() - begin - 1 < std::numeric_limits<uint32_t>::max());
        output[begin].metadata.y = static_cast<uint32_t>(output.size() - begin - 1);
        return begin;
    }

private:
    std::vector<pbrt::TextureInstruction>& output;
    const AssetManager&                    assets;
    const AssetDataManager&                assetData;
    MaterialTable&                         materialTable;
    std::unordered_set<std::string>        activeTextures;
    uint32_t                               stackDepth = 0;

    static pbrt::TextureInstruction node(pbrt::TextureOp op, const PbrtTextureMapping& mapping = {})
    {
        pbrt::TextureInstruction instruction;
        instruction.metadata.x    = std::to_underlying(op);
        instruction.uvScaleOffset = glm::vec4{glm::vec2{mapping.scale}, glm::vec2{mapping.offset}};
        instruction.uAxisKind =
            glm::vec4{glm::vec3{mapping.uAxis}, static_cast<float>(std::to_underlying(textureMapping(mapping.kind)))};
        instruction.vAxis            = glm::vec4{glm::vec3{mapping.vAxis}, 0};
        instruction.textureFromWorld = glm::mat4{mapping.textureFromWorld};
        return instruction;
    }

    void emit(const pbrt::TextureInstruction& instruction, uint32_t operands)
    {

        stackDepth = stackDepth - operands + 1;
        CHECK(
            stackDepth <= pbrt::kTextureStackSize,
            "PBRT RGB texture expression exceeds {} stack values",
            pbrt::kTextureStackSize
        );
        output.push_back(instruction);
    }
    void constant(glm::vec3 value)
    {
        auto instruction  = node(pbrt::TextureOp::Constant);
        instruction.value = glm::vec4{value, 0};
        emit(instruction, 0);
    }

    template <class Input>
    void input(const Input& source, PbrtTextureType type)
    {
        std::visit(
            [&]<class T>(const T& value)
            {
                if constexpr (std::same_as<T, PbrtTextureReference>)
                    texture(value.id, type);
                else if constexpr (std::same_as<T, PbrtFloatConstant>)
                    constant(glm::vec3{static_cast<float>(value.value)});
                else
                    constant(pbrtPreviewRgb(PbrtSpectrum{value}, assetData));
            },
            source
        );
    }

    void texture(const PbrtTextureAsset::ID& id, PbrtTextureType expected)
    {
        CHECK(activeTextures.insert(id.value).second, "cyclic PBRT texture graph: {}", id);
        const auto& source = assets.get(id);
        CHECK(source.valueType == expected, "PBRT texture type mismatch: {}", id);
        const auto pushInput = [&](const auto& value) { input(value, source.valueType); };
        std::visit(
            [&]<class T>(const T& model)
            {
                pbrt::TextureInstruction instruction;
                uint32_t                 operands = 0;
                if constexpr (std::same_as<T, PbrtTextureAsset::Constant>)
                {
                    pushInput(model.value);
                    return;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::ImageMap>)
                {
                    instruction            = node(pbrt::TextureOp::Image, source.mapping);
                    const auto image       = materialTable.textureReference(model.image);
                    instruction.metadata.z = image.textureIndex;
                    instruction.metadata.w = image.samplerIndex;
                    instruction.value      = {static_cast<float>(model.scale), model.invert ? 1.0f : 0.0f, 0, 0};
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Scale>)
                {
                    pushInput(model.texture);
                    input(model.scale, PbrtTextureType::Float);
                    instruction = node(pbrt::TextureOp::Multiply);
                    operands    = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Mix>)
                {
                    pushInput(model.first);
                    pushInput(model.second);
                    input(model.amount, PbrtTextureType::Float);
                    instruction = node(pbrt::TextureOp::Mix);
                    operands    = 3;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::DirectionMix>)
                {
                    pushInput(model.first);
                    pushInput(model.second);
                    instruction       = node(pbrt::TextureOp::DirectionMix);
                    instruction.value = glm::vec4{glm::vec3{model.direction}, 0};
                    operands          = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Bilerp>)
                {
                    pushInput(model.v00);
                    pushInput(model.v01);
                    pushInput(model.v10);
                    pushInput(model.v11);
                    instruction = node(pbrt::TextureOp::Bilerp, source.mapping);
                    operands    = 4;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Checkerboard>)
                {
                    pushInput(model.first);
                    pushInput(model.second);
                    instruction            = node(pbrt::TextureOp::Checkerboard, source.mapping);
                    instruction.settings.x = static_cast<float>(model.dimension);
                    operands               = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Dots>)
                {
                    pushInput(model.inside);
                    pushInput(model.outside);
                    instruction = node(pbrt::TextureOp::Dots, source.mapping);
                    operands    = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Noise>)
                {
                    instruction = node(
                        model.kind == T::Kind::Fbm            ? pbrt::TextureOp::Fbm
                            : model.kind == T::Kind::Wrinkled ? pbrt::TextureOp::Wrinkled
                                                              : pbrt::TextureOp::Windy,
                        source.mapping
                    );
                    constexpr int32_t kMaxPreviewNoiseOctaves = 12;
                    instruction.settings                      = {
                        static_cast<float>(model.roughness),
                        static_cast<float>(std::min(model.octaves, kMaxPreviewNoiseOctaves)),
                        0,
                        0
                    };
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Marble>)
                {
                    instruction                               = node(pbrt::TextureOp::Marble, source.mapping);
                    constexpr int32_t kMaxPreviewNoiseOctaves = 12;
                    instruction.settings                      = {
                        static_cast<float>(model.roughness),
                        static_cast<float>(std::min(model.octaves, kMaxPreviewNoiseOctaves)),
                        0,
                        0
                    };
                    instruction.value = {static_cast<float>(model.scale), static_cast<float>(model.variation), 0, 0};
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Ptex>)
                {
                    instruction         = node(pbrt::TextureOp::PtexPreview);
                    instruction.value.x = static_cast<float>(model.scale);
                }
                instruction.metadata.y = source.valueType == PbrtTextureType::Float ? 1u : 0u;
                emit(instruction, operands);
            },
            source.model
        );
        activeTextures.erase(id.value);
    }
};

class MaterialCompiler
{
public:
    MaterialCompiler(
        std::vector<pbrt::MaterialInstruction>& materialCode,
        std::vector<pbrt::TextureInstruction>&  textureCode,
        const AssetManager&                     assets,
        const AssetDataManager&                 assetData,
        MaterialTable&                          materialTable
    ) :
        output(materialCode), assets(assets), textures(textureCode, assets, assetData, materialTable)
    {
    }

    uint32_t compile(const MaterialAsset::ID& id)
    {
        if (const auto found = materialIndices.find(id.value); found != materialIndices.end())
            return found->second;
        CHECK(output.size() < std::numeric_limits<uint32_t>::max());
        const auto begin = static_cast<uint32_t>(output.size());
        headerIndex      = begin;
        output.push_back({.model = pbrt::MaterialModel::Diffuse});
        stackDepth = 0;
        append(assets.get(id).as<PbrtMaterial>());
        if (output[begin].model != pbrt::MaterialModel::Diffuse)
        {

            output[begin].instructionCount = 0;
        }
        else
        {

            output[begin].model = pbrt::MaterialModel::Diffuse;
            CHECK(output.size() - begin - 1 < std::numeric_limits<uint32_t>::max());
            output[begin].instructionCount = static_cast<uint32_t>(output.size() - begin - 1);
        }
        materialIndices.emplace(id.value, begin);
        return begin;
    }

private:
    std::vector<pbrt::MaterialInstruction>&   output;
    const AssetManager&                       assets;
    TextureCompiler                           textures;
    std::unordered_map<std::string, uint32_t> materialIndices;
    std::unordered_set<std::string>           activeMaterials;
    uint32_t                                  stackDepth  = 0;
    uint32_t                                  headerIndex = 0;

    uint32_t spectrum(const PbrtSpectrumInput& value) { return textures.compile(value, PbrtTextureType::Spectrum); }
    uint32_t spectrum(const PbrtSpectrum& value)
    {
        return std::visit([&](const auto& spectrumValue) { return spectrum(PbrtSpectrumInput{spectrumValue}); }, value);
    }
    uint32_t scalar(const PbrtFloatInput& value) { return textures.compile(value, PbrtTextureType::Float); }

    static bool constantZero(const PbrtFloatInput& value)
    {
        const auto* constant = std::get_if<PbrtFloatConstant>(&value);
        return constant && constant->value == 0.0;
    }

    static bool smoothRoughness(const PbrtMaterial::Roughness& roughness)
    {
        return constantZero(roughness.u) && constantZero(roughness.v);
    }

    void emit(const pbrt::MaterialInstruction& material, uint32_t operands = 0)
    {

        stackDepth = stackDepth - operands + 1;
        CHECK(
            stackDepth <= pbrt::kMaterialStackSize,
            "PBRT material mix exceeds {} stack values",
            pbrt::kMaterialStackSize
        );
        output.push_back(material);
    }

    void color(const PbrtSpectrumInput& value)
    {
        emit({.model = pbrt::MaterialModel::Color, .firstTexture = spectrum(value)});
    }

    void conductorColor(const PbrtMaterial::ConductorOptics& optics)
    {
        std::visit(
            [&]<class T>(const T& value)
            {
                if constexpr (std::same_as<T, PbrtMaterial::Reflectance>)
                    color(value.value);
                else
                {
                    emit(
                        {.model         = pbrt::MaterialModel::ConductorColor,
                         .firstTexture  = spectrum(value.eta),
                         .secondTexture = spectrum(value.k)}
                    );
                }
            },
            optics
        );
    }

    void conductor(const PbrtMaterial::Conductor& source)
    {
        pbrt::MaterialInstruction material{
            .model  = pbrt::MaterialModel::Conductor,
            .inputs = {scalar(source.roughness.u), scalar(source.roughness.v), source.remapRoughness ? 1u : 0u, 0},
        };
        std::visit(
            [&]<class T>(const T& optics)
            {
                if constexpr (std::same_as<T, PbrtMaterial::Reflectance>)
                {
                    material.firstTexture = spectrum(optics.value);
                    material.inputs.w     = 1;
                }
                else
                {
                    material.firstTexture  = spectrum(optics.eta);
                    material.secondTexture = spectrum(optics.k);
                }
            },
            source.optics
        );
        output[headerIndex] = material;
    }

    void append(const PbrtMaterial& source)
    {
        CHECK(activeMaterials.insert(source.id.value).second, "cyclic PBRT material graph: {}", source.id);
        std::visit(
            [&]<class T>(const T& model)
            {
                if constexpr (std::same_as<T, PbrtMaterial::Diffuse> || std::same_as<T, PbrtMaterial::CoatedDiffuse>)
                    color(model.reflectance);
                else if constexpr (std::same_as<T, PbrtMaterial::Dielectric>)
                {
                    if (smoothRoughness(model.roughness) && activeMaterials.size() == 1)
                    {
                        output[headerIndex].model        = pbrt::MaterialModel::SmoothDielectric;
                        output[headerIndex].firstTexture = spectrum(model.eta);
                    }
                    else
                        color(PbrtConstantSpectrum{1.0});
                }
                else if constexpr (std::same_as<T, PbrtMaterial::Conductor>)
                {
                    if (activeMaterials.size() == 1)
                        conductor(model);
                    else
                        conductorColor(model.optics);
                }
                else if constexpr (std::same_as<T, PbrtMaterial::CoatedConductor>)
                    conductorColor(model.optics);
                else if constexpr (std::same_as<T, PbrtMaterial::DiffuseTransmission>)
                {
                    emit(
                        {.model         = pbrt::MaterialModel::DiffuseTransmission,
                         .firstTexture  = spectrum(model.reflectance),
                         .secondTexture = spectrum(model.transmittance),
                         .inputs        = {scalar(PbrtFloatConstant{model.scale}), 0, 0, 0}}
                    );
                }
                else if constexpr (std::same_as<T, PbrtMaterial::Mix>)
                {
                    append(assets.get(model.first).template as<PbrtMaterial>());
                    append(assets.get(model.second).template as<PbrtMaterial>());
                    emit({.model = pbrt::MaterialModel::Mix, .firstTexture = scalar(model.amount)}, 2);
                }
                else if constexpr (std::same_as<T, PbrtMaterial::Subsurface>)
                    std::visit(
                        [&]<class S>(const S& scattering)
                        {
                            if constexpr (std::same_as<S, PbrtMaterial::ScatteringReflectance>)
                                color(scattering.reflectance);
                            else if constexpr (std::same_as<S, PbrtMaterial::ScatteringCoefficients>)
                            {
                                emit(
                                    {.model         = pbrt::MaterialModel::Subsurface,
                                     .firstTexture  = spectrum(scattering.sigmaA),
                                     .secondTexture = spectrum(scattering.sigmaS)}
                                );
                            }
                            else
                                color(PbrtConstantSpectrum{0.5});
                        },
                        model.scattering
                    );
                else if constexpr (std::same_as<T, PbrtMaterial::Hair>)
                    std::visit(
                        [&]<class S>(const S& absorption)
                        {
                            if constexpr (std::same_as<S, PbrtMaterial::Reflectance>)
                                color(absorption.value);
                            else if constexpr (std::same_as<S, PbrtMaterial::HairAbsorption>)
                            {
                                emit(
                                    {.model        = pbrt::MaterialModel::HairAbsorption,
                                     .firstTexture = spectrum(absorption.sigmaA)}
                                );
                            }
                            else
                            {
                                emit(
                                    {.model         = pbrt::MaterialModel::HairPigments,
                                     .firstTexture  = scalar(absorption.eumelanin),
                                     .secondTexture = scalar(absorption.pheomelanin)}
                                );
                            }
                        },
                        model.absorption
                    );
                else if constexpr (std::same_as<T, PbrtMaterial::Measured>)
                    color(PbrtConstantSpectrum{0.5});
                else
                    color(PbrtConstantSpectrum{1.0});
            },
            source.model
        );
        activeMaterials.erase(source.id.value);
    }
};

}  // namespace


void MaterialTable::init(rhi::IDevice& targetDevice, RenderResourceManager& targetResources)
{
    device = &targetDevice;
    resources = &targetResources;
    static_cast<void>(textureReference({BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat}));
    samplers.fill(samplers[0]);

    const auto createBuffer = [&](size_t size)
    {
        rhi::ComPtr<rhi::IBuffer> buffer;
        rhiCheck(device->createBuffer(
            {.size = size, .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
            nullptr,
            buffer.writeRef()
        ));
        return buffer;
    };
    materialBuffer   = createBuffer(sizeof(MaterialData));
    materialPrograms = createBuffer(sizeof(pbrt::MaterialInstruction));
    texturePrograms  = createBuffer(sizeof(pbrt::TextureInstruction));
    uploadedMaterialRevision.reset();
    uploadedProgramRevision.reset();
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
        textureBindings.clear();

    return {.textureIndex = image->second, .samplerIndex = sampler->second};
}

size_t MaterialTable::MaterialKeyHash::operator()(const MaterialKey& key) const noexcept
{
    size_t     hash    = std::hash<AssetID<MaterialAsset>>{}(key.assetId);
    const auto combine = [&hash](uint32_t value)
    {
        constexpr size_t kHashCombineSeed = 0x9e3779b9;
        hash ^= std::hash<uint32_t>{}(value) + kHashCombineSeed + (hash << 6) + (hash >> 2);
    };
    combine(key.overrideOwner.has_value());
    if (key.overrideOwner)
    {
        combine(key.overrideOwner->objectId);
        combine(key.overrideOwner->instanceIndex);
        combine(key.overrideOwner->submeshIndex);
    }
    return hash;
}


std::vector<uint32_t>
MaterialTable::prepare(std::span<const DrawItem> items, const AssetManager& assets, const AssetDataManager& assetData)
{
    const auto previousMaterialCode = std::exchange(materialCode, {});
    const auto previousTextureCode = std::exchange(textureCode, {});
    MaterialCompiler compiler(materialCode, textureCode, assets, assetData, *this);

    std::vector<uint32_t> indices;
    indices.reserve(items.size());
    for (const auto& item : items)
    {
        const auto& source = assets.get(item.materialId);
        MaterialKey key{.assetId = item.materialId};

        if (const auto* gltf = dynamic_cast<const GltfMaterial*>(&source))
        {
            if (item.materialOverride)
            {
                auto material = *gltf;
                applyOptionalFields(material, *item.materialOverride);
                key.overrideOwner = {item.selectionId, item.instanceIndex, item.submeshIndex};
                indices.push_back(updateGltf(key, material, assets));
            }
            else
                indices.push_back(updateGltf(key, *gltf, assets));
        }
        else
        {
            CHECK(!item.materialOverride, "glTF material override on PBRT material '{}'", item.materialId);
            MaterialData data{.doubleSided = 1, .kind = MaterialKind::Pbrt, .programIndex = compiler.compile(item.materialId)};
            if (const auto& emission = source.as<PbrtMaterial>().emission)
            {
                const auto& light = emission->light;
                const auto scale = static_cast<float>(light.scale);
                if (light.image)
                {
                    data.emissiveFactor = {glm::vec3{scale}, 0.0f};
                    data.emissiveTexture = textureReference(light.image->image);
                    data.emissiveFlags |= kEmissiveFlipUv;
                }
                else
                    data.emissiveFactor = {pbrtPreviewRgb(light.spectrum, assetData) * scale, 0.0f};
                if (emission->twoSided)
                    data.emissiveFlags |= kEmissiveTwoSided;
            }
            indices.push_back(store(key, data, {
                .alphaMode = kAlphaModeOpaque,
                .doubleSided = true,
                .emissive = glm::any(glm::greaterThan(glm::vec3{data.emissiveFactor}, glm::vec3{0.0f})),
            }));
        }
    }

    if (previousMaterialCode != materialCode || previousTextureCode != textureCode)
        ++programRevision;
    return indices;
}

uint32_t MaterialTable::updateGltf(const MaterialKey& key, const GltfMaterial& material, const AssetManager& assets)
{
    CHECK(material.workflow.has_value(), "material '{}' requires a workflow", material.id);
    CHECK(
        *material.workflow == MaterialWorkflow::MetallicRoughness ||
            *material.workflow == MaterialWorkflow::SpecularGlossiness,
        "invalid material workflow: {}",
        material.id
    );
    const bool sgWorkflow = *material.workflow == MaterialWorkflow::SpecularGlossiness;
    CHECK(!sgWorkflow || material.specularGlossiness.has_value(), "SG parameters missing: {}", material.id);

    const auto alphaMode = material.alphaMode.value_or("OPAQUE");
    CHECK(
        alphaMode == "OPAQUE" || alphaMode == "MASK" || alphaMode == "BLEND",
        "material '{}' has an unknown alpha mode '{}'",
        material.id,
        alphaMode
    );
    const float emissiveStrength = material.emissiveStrength.value_or(1.0f);
    CHECK(std::isfinite(emissiveStrength) && emissiveStrength >= 0.0f);

    const auto  clearcoat = material.clearcoat.value_or(GltfMaterial::Clearcoat{});
    const auto  sheen     = material.sheen.value_or(GltfMaterial::Sheen{});
    const auto  specular  = material.specular.value_or(GltfMaterial::Specular{});
    const float ior       = material.ior.value_or(1.5f);
    CHECK(std::isfinite(ior) && (ior == 0.0f || ior >= 1.0f), "invalid IOR in material '{}'", material.id);
    CHECK(
        std::isfinite(specular.factor) && specular.factor >= 0.0f && specular.factor <= 1.0f,
        "invalid specular factor in material '{}'",
        material.id
    );
    for (glm::length_t channel = 0; channel < 3; ++channel)
    {
        CHECK(
            std::isfinite(specular.colorFactor[channel]) && specular.colorFactor[channel] >= 0.0f,
            "invalid specular color in material '{}'",
            material.id
        );
    }
    const float iorRatio = ior == 0.0f ? 1.0f : (ior - 1.0f) / (ior + 1.0f);

    MaterialData data{
        .colorFactor              = material.baseColorFactor.value_or(glm::vec4{1.0f}),
        .roughness                = material.roughness.value_or(1.0f),
        .metallic                 = material.metallic.value_or(1.0f),
        .alphaCutoff              = material.alphaCutoff.value_or(0.5f),
        .alphaMode                = alphaMode == "MASK" ? kAlphaModeMask
                           : alphaMode == "BLEND"       ? kAlphaModeBlend
                                                        : kAlphaModeOpaque,
        .normalScale              = material.normalScale.value_or(1.0f),
        .occlusionStrength        = material.ao.value_or(1.0f),
        .doubleSided              = material.doubleSided.value_or(true) ? 1u : 0u,
        .workflow                 = static_cast<uint32_t>(*material.workflow),
        .emissiveFactor           = glm::vec4(material.emissive.value_or(glm::vec3{0.0f}) * emissiveStrength, 0.0f),
        .sheenColorFactor         = sheen.colorFactor,
        .sheenRoughnessFactor     = sheen.roughnessFactor,
        .clearcoatFactor          = clearcoat.factor,
        .clearcoatRoughnessFactor = clearcoat.roughnessFactor,
        .clearcoatNormalScale     = clearcoat.normalScale,
        .specularColorFactor      = specular.colorFactor,
        .specularFactor           = specular.factor,
        .dielectricF0             = iorRatio * iorRatio,
    };

    const auto resolveTexture = [&](const std::optional<TextureBinding>& source,
                                    const TextureAsset::ID&              fallback,
                                    std::optional<ColorSpace>            colorSpace = std::nullopt)
    {
        const auto binding = source.value_or(TextureBinding{fallback, BuiltinAssets::Sampler::linearRepeat});
        return textureReference(binding);
    };

    if (sgWorkflow)
    {
        const auto& sg = *material.specularGlossiness;
        CHECK(
            std::isfinite(sg.glossinessFactor) && sg.glossinessFactor >= 0.0f && sg.glossinessFactor <= 1.0f,
            "invalid glossiness in material '{}'",
            material.id
        );
        for (glm::length_t channel = 0; channel < 4; ++channel)
        {
            CHECK(
                std::isfinite(sg.diffuseFactor[channel]) && sg.diffuseFactor[channel] >= 0.0f &&
                    sg.diffuseFactor[channel] <= 1.0f,
                "invalid SG diffuse color in material '{}'",
                material.id
            );
        }
        for (glm::length_t channel = 0; channel < 3; ++channel)
        {
            CHECK(
                std::isfinite(sg.specularFactor[channel]) && sg.specularFactor[channel] >= 0.0f &&
                    sg.specularFactor[channel] <= 1.0f,
                "invalid SG specular color in material '{}'",
                material.id
            );
        }

        data.colorFactor          = sg.diffuseFactor;
        data.specularColorFactor  = sg.specularFactor;
        data.glossinessFactor     = sg.glossinessFactor;
        data.colorTexture         = resolveTexture(sg.diffuseTexture, BuiltinAssets::Texture::white);
        data.specularColorTexture = resolveTexture(sg.specularGlossinessTexture, BuiltinAssets::Texture::white);
    }
    else
    {
        data.colorTexture         = resolveTexture(material.baseColorTexture, BuiltinAssets::Texture::white);
        data.metallicTexture      = resolveTexture(material.metallicTexture, BuiltinAssets::Texture::kWhiteLinear);
        data.roughnessTexture     = resolveTexture(material.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear);
        data.specularColorTexture = resolveTexture(specular.colorTexture, BuiltinAssets::Texture::white);
        data.specularTexture      = resolveTexture(specular.texture, BuiltinAssets::Texture::kWhiteLinear);
    }

    data.normalTexture = resolveTexture(material.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear);
    data.normalEncoding  = normalEncoding(material.normalTexture, assets);
    data.emissiveTexture = resolveTexture(material.emissiveTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb);
    if (data.doubleSided)
        data.emissiveFlags = kEmissiveTwoSided;
    data.occlusionTexture =
        resolveTexture(material.aoTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.sheenColorTexture = resolveTexture(sheen.colorTexture, BuiltinAssets::Texture::white, ColorSpace::Srgb);
    data.sheenRoughnessTexture =
        resolveTexture(sheen.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatTexture = resolveTexture(clearcoat.texture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatRoughnessTexture =
        resolveTexture(clearcoat.roughnessTexture, BuiltinAssets::Texture::kWhiteLinear, ColorSpace::Linear);
    data.clearcoatNormalTexture =
        resolveTexture(clearcoat.normalTexture, BuiltinAssets::Texture::flatNormal, ColorSpace::Linear);
    data.clearcoatNormalEncoding = normalEncoding(clearcoat.normalTexture, assets);

    return store(key, data, {
        .alphaMode   = data.alphaMode,
        .doubleSided = data.doubleSided != 0,
        .clearcoat   = material.clearcoat.has_value(),
        .sheen       = material.sheen.has_value(),
        .emissive    = glm::any(glm::greaterThan(glm::vec3{data.emissiveFactor}, glm::vec3{0.0f})),
    });
}

uint32_t MaterialTable::store(const MaterialKey& key, const MaterialData& data, MaterialInfo info)
{
    const auto [found, added] = materialIndices.try_emplace(key, static_cast<uint32_t>(materials.size()));
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
            (info.emissive && (previous.emissiveFactor != data.emissiveFactor ||
                previous.emissiveTexture != data.emissiveTexture || previous.emissiveFlags != data.emissiveFlags)))
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
        textureBindings.clear();
    }

    const auto uploadTable = [&](rhi::ComPtr<rhi::IBuffer>& buffer, const auto values)
    {
        using Value = typename decltype(values)::value_type;
        const std::array<std::byte, sizeof(Value)> empty{};
        const auto bytes = values.empty() ? std::span<const std::byte>{empty} : std::as_bytes(values);
        ensureBufferCapacity(*device, buffer, bytes.size());
        rhiCheck(commands.uploadBufferData(buffer, 0, bytes.size(), bytes.data()));
    };

    const bool materialsChanged = uploadedMaterialRevision != materialRevision;
    if (materialsChanged)
    {
        uploadTable(materialBuffer, std::span{materials});
        uploadedMaterialRevision = materialRevision;
    }
    const bool programsChanged = uploadedProgramRevision != programRevision;
    if (programsChanged)
    {
        uploadTable(materialPrograms, std::span{materialCode});
        uploadTable(texturePrograms, std::span{textureCode});
        uploadedProgramRevision = programRevision;
    }
    return materialsChanged || programsChanged || imagesChanged;
}

void MaterialTable::bind(rhi::IShaderObject& object) const
{
    const rhi::ShaderCursor root(&object);
    rhiCheck(root["materials"].setBinding(materialBuffer));
    if (const auto field = root["materialInstructions"]; field.isValid())
        rhiCheck(field.setBinding(materialPrograms));
    if (const auto field = root["textureInstructions"]; field.isValid())
        rhiCheck(field.setBinding(texturePrograms));
    const auto textures = root["materialTextures"];
    auto* layout = textures.getTypeLayout()->getElementTypeLayout();
    auto& binding = textureBindings[layout];
    if (!binding)
    {
        rhiCheck(device->createShaderObjectFromTypeLayout(object.getSlangComponentType(), layout, binding.writeRef()));
        const rhi::ShaderCursor table(binding);
        for (uint32_t index = 0; index < images.size(); ++index)
            rhiCheck(table["textures"][index].setBinding(images[index]));
        for (uint32_t index = 0; index < samplers.size(); ++index)
            rhiCheck(table["samplers"][index].setBinding(samplers[index]));
        rhiCheck(binding->finalize());
    }
    rhiCheck(textures.setObject(binding));
}
