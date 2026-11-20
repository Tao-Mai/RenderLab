#include "render/raytracing/offline/OfflineScene.h"

#include "asset/AssetManager.h"
#include "asset/AssetDataManager.h"
#include "asset/pbrt/PbrtData.h"
#include "asset/pbrt/PbrtMaterial.h"
#include "core/Context.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace
{
glm::vec3 previewRgb(const PbrtSpectrum& spectrum)
{
    return std::visit(
        []<class T>(const T& value) -> glm::vec3
        {
            if constexpr (std::same_as<T, PbrtConstantSpectrum>)
                return glm::vec3(value.value);
            else if constexpr (std::same_as<T, PbrtRgbSpectrum>)
                return glm::vec3(value.value);
            else if constexpr (std::same_as<T, PbrtBlackbodySpectrum>)
            {
                if (value.temperature <= 0.0)
                    return glm::vec3{0.0f};
                constexpr double kMinimumPreviewTemperature = 100.0;
                constexpr double kMaximumPreviewTemperature = 1e9;
                const double     temperature =
                    std::clamp(value.temperature, kMinimumPreviewTemperature, kMaximumPreviewTemperature);
                constexpr double     kSecondRadiationConstantNm = 1.438776877e7;
                constexpr glm::dvec3 kPreviewWavelengths{610, 550, 460};
                glm::dvec3           rgb;
                for (int channel = 0; channel < 3; ++channel)
                {
                    const auto wavelength = kPreviewWavelengths[channel];
                    rgb[channel]          = 1.0 /
                        (std::pow(wavelength, 5) * std::expm1(kSecondRadiationConstantNm / (wavelength * temperature)));
                }
                return glm::vec3(rgb / std::max({rgb.x, rgb.y, rgb.z}));
            }
            else if constexpr (std::same_as<T, PbrtSampledSpectrum>)
            {
                const auto samples = std::visit(
                    []<class Array>(const Array& data) -> PbrtFloatValues
                    {
                        if constexpr (std::same_as<Array, PbrtFloatValues>)
                            return data;
                        else
                            return std::get<PbrtFloatValues>(readPbrtArray(*context().assetDataManager, data));
                    },
                    value.samples
                );
                CHECK(samples.values.size() >= 2 && samples.values.size() % 2 == 0);
                if (samples.values.size() == 2)
                    return glm::vec3{static_cast<float>(samples.values[1])};
                const auto evaluate = [&](double wavelength)
                {
                    if (wavelength < samples.values.front() || wavelength > samples.values[samples.values.size() - 2])
                        return 0.0;
                    for (size_t index = 2; index < samples.values.size(); index += 2)
                        if (wavelength <= samples.values[index])
                        {
                            const auto t = (wavelength - samples.values[index - 2]) /
                                (samples.values[index] - samples.values[index - 2]);
                            return std::lerp(samples.values[index - 1], samples.values[index + 1], t);
                        }
                    std::unreachable();
                };
                return {
                    static_cast<float>(evaluate(610)),
                    static_cast<float>(evaluate(550)),
                    static_cast<float>(evaluate(460))
                };
            }
            // Named spectra and external SPDs retain their complete assets; the RGB preview uses neutral white.
            else
                return glm::vec3{1.0f};
        },
        spectrum
    );
}

OfflineTextureMapping textureMapping(PbrtTextureMapping::Kind kind)
{
    switch (kind)
    {
        case PbrtTextureMapping::Kind::Uv:
            return OfflineTextureMapping::Uv;
        case PbrtTextureMapping::Kind::Spherical:
            return OfflineTextureMapping::Spherical;
        case PbrtTextureMapping::Kind::Cylindrical:
            return OfflineTextureMapping::Cylindrical;
        case PbrtTextureMapping::Kind::Planar:
            return OfflineTextureMapping::Planar;
        case PbrtTextureMapping::Kind::Spatial:
            return OfflineTextureMapping::Spatial;
    }
    std::unreachable();
}

class TextureCompiler
{
public:
    TextureCompiler(
        std::vector<OfflineTextureInstruction>& output,
        AssetManager&                           assets,
        RenderResourceManager&                  resources,
        MaterialTable&                          materialTable
    ) :
        output(output), assets(assets), resources(resources), materialTable(materialTable)
    {
    }

    template <class Input>
    uint32_t compile(const Input& source, PbrtTextureType type)
    {
        CHECK(output.size() < std::numeric_limits<uint32_t>::max());
        const auto begin = static_cast<uint32_t>(output.size());
        output.push_back(node(OfflineTextureOp::Program));
        stackDepth = 0;
        input(source, type);
        DCHECK(stackDepth == 1);
        CHECK(output.size() - begin - 1 < std::numeric_limits<uint32_t>::max());
        output[begin].metadata.y = static_cast<uint32_t>(output.size() - begin - 1);
        return begin;
    }

private:
    std::vector<OfflineTextureInstruction>& output;
    AssetManager&                           assets;
    RenderResourceManager&                  resources;
    MaterialTable&                          materialTable;
    std::unordered_set<std::string>         activeTextures;
    uint32_t                                stackDepth = 0;

    static OfflineTextureInstruction node(OfflineTextureOp op, const PbrtTextureMapping& mapping = {})
    {
        OfflineTextureInstruction instruction;
        instruction.metadata.x    = std::to_underlying(op);
        instruction.uvScaleOffset = glm::vec4{glm::vec2{mapping.scale}, glm::vec2{mapping.offset}};
        instruction.uAxisKind =
            glm::vec4{glm::vec3{mapping.uAxis}, static_cast<float>(std::to_underlying(textureMapping(mapping.kind)))};
        instruction.vAxis            = glm::vec4{glm::vec3{mapping.vAxis}, 0};
        instruction.textureFromWorld = glm::mat4{mapping.textureFromWorld};
        return instruction;
    }

    void emit(const OfflineTextureInstruction& instruction, uint32_t operands)
    {
        DCHECK(stackDepth >= operands);
        stackDepth = stackDepth - operands + 1;
        CHECK(
            stackDepth <= kOfflineTextureStackSize,
            "offline RGB texture expression exceeds {} stack values",
            kOfflineTextureStackSize
        );
        output.push_back(instruction);
    }
    void constant(glm::vec3 value)
    {
        auto instruction  = node(OfflineTextureOp::Constant);
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
                    constant(previewRgb(PbrtSpectrum{value}));
            },
            source
        );
    }

    void texture(const PbrtTextureAsset::ID& id, PbrtTextureType expected)
    {
        CHECK(activeTextures.insert(id.value).second, "cyclic offline texture graph: {}", id);
        const auto& source = assets.get(id);
        CHECK(source.valueType == expected, "offline texture type mismatch: {}", id);
        const auto pushInput = [&](const auto& value) { input(value, source.valueType); };
        std::visit(
            [&]<class T>(const T& model)
            {
                OfflineTextureInstruction instruction;
                uint32_t                  operands = 0;
                if constexpr (std::same_as<T, PbrtTextureAsset::Constant>)
                {
                    pushInput(model.value);
                    return;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::ImageMap>)
                {
                    instruction            = node(OfflineTextureOp::Image, source.mapping);
                    const auto image       = materialTable.textureReference(model.image, resources);
                    instruction.metadata.z = image.textureIndex;
                    instruction.metadata.w = image.samplerIndex;
                    instruction.value      = {static_cast<float>(model.scale), model.invert ? 1.0f : 0.0f, 0, 0};
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Scale>)
                {
                    pushInput(model.texture);
                    input(model.scale, PbrtTextureType::Float);
                    instruction = node(OfflineTextureOp::Multiply);
                    operands    = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Mix>)
                {
                    pushInput(model.first);
                    pushInput(model.second);
                    input(model.amount, PbrtTextureType::Float);
                    instruction = node(OfflineTextureOp::Mix);
                    operands    = 3;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::DirectionMix>)
                {
                    pushInput(model.first);
                    pushInput(model.second);
                    instruction       = node(OfflineTextureOp::DirectionMix);
                    instruction.value = glm::vec4{glm::vec3{model.direction}, 0};
                    operands          = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Bilerp>)
                {
                    pushInput(model.v00);
                    pushInput(model.v01);
                    pushInput(model.v10);
                    pushInput(model.v11);
                    instruction = node(OfflineTextureOp::Bilerp, source.mapping);
                    operands    = 4;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Checkerboard>)
                {
                    pushInput(model.first);
                    pushInput(model.second);
                    instruction            = node(OfflineTextureOp::Checkerboard, source.mapping);
                    instruction.settings.x = static_cast<float>(model.dimension);
                    operands               = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Dots>)
                {
                    pushInput(model.inside);
                    pushInput(model.outside);
                    instruction = node(OfflineTextureOp::Dots, source.mapping);
                    operands    = 2;
                }
                else if constexpr (std::same_as<T, PbrtTextureAsset::Noise>)
                {
                    instruction = node(
                        model.kind == T::Kind::Fbm            ? OfflineTextureOp::Fbm
                            : model.kind == T::Kind::Wrinkled ? OfflineTextureOp::Wrinkled
                                                              : OfflineTextureOp::Windy,
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
                    instruction                               = node(OfflineTextureOp::Marble, source.mapping);
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
                    instruction         = node(OfflineTextureOp::PtexPreview);
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
        OfflineSceneData& output, AssetManager& assets, RenderResourceManager& resources, MaterialTable& materialTable
    ) :
        output(output.materials), assets(assets), textures(output.textures, assets, resources, materialTable)
    {
    }

    uint32_t compile(const MaterialAsset::ID& id)
    {
        if (const auto found = materialIndices.find(id.value); found != materialIndices.end())
            return found->second;
        CHECK(output.size() < std::numeric_limits<uint32_t>::max());
        const auto begin = static_cast<uint32_t>(output.size());
        headerIndex      = begin;
        output.push_back({.model = OfflineMaterialPreviewModel::Diffuse});
        stackDepth = 0;
        append(assets.get(id).as<PbrtMaterial>());
        if (output[begin].model != OfflineMaterialPreviewModel::Diffuse)
        {
            DCHECK(stackDepth == 0);
            output[begin].instructionCount = 0;
        }
        else
        {
            DCHECK(stackDepth == 1);
            output[begin].model = OfflineMaterialPreviewModel::Diffuse;
            CHECK(output.size() - begin - 1 < std::numeric_limits<uint32_t>::max());
            output[begin].instructionCount = static_cast<uint32_t>(output.size() - begin - 1);
        }
        materialIndices.emplace(id.value, begin);
        return begin;
    }

private:
    std::vector<OfflineMaterialPreview>&      output;
    AssetManager&                             assets;
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

    void emit(const OfflineMaterialPreview& material, uint32_t operands = 0)
    {
        DCHECK(stackDepth >= operands);
        stackDepth = stackDepth - operands + 1;
        CHECK(
            stackDepth <= kOfflineMaterialStackSize,
            "offline material mix exceeds {} stack values",
            kOfflineMaterialStackSize
        );
        output.push_back(material);
    }

    void color(const PbrtSpectrumInput& value)
    {
        emit({.model = OfflineMaterialPreviewModel::Color, .firstTexture = spectrum(value)});
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
                        {.model         = OfflineMaterialPreviewModel::ConductorColor,
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
        OfflineMaterialPreview material{
            .model  = OfflineMaterialPreviewModel::Conductor,
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
        CHECK(activeMaterials.insert(source.id.value).second, "cyclic offline material graph: {}", source.id);
        std::visit(
            [&]<class T>(const T& model)
            {
                if constexpr (std::same_as<T, PbrtMaterial::Diffuse> || std::same_as<T, PbrtMaterial::CoatedDiffuse>)
                    color(model.reflectance);
                else if constexpr (std::same_as<T, PbrtMaterial::Dielectric>)
                {
                    if (smoothRoughness(model.roughness) && activeMaterials.size() == 1)
                    {
                        output[headerIndex].model        = OfflineMaterialPreviewModel::SmoothDielectric;
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
                        {.model         = OfflineMaterialPreviewModel::DiffuseTransmission,
                         .firstTexture  = spectrum(model.reflectance),
                         .secondTexture = spectrum(model.transmittance),
                         .inputs        = {scalar(PbrtFloatConstant{model.scale}), 0, 0, 0}}
                    );
                }
                else if constexpr (std::same_as<T, PbrtMaterial::Mix>)
                {
                    append(assets.get(model.first).template as<PbrtMaterial>());
                    append(assets.get(model.second).template as<PbrtMaterial>());
                    emit({.model = OfflineMaterialPreviewModel::Mix, .firstTexture = scalar(model.amount)}, 2);
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
                                    {.model         = OfflineMaterialPreviewModel::Subsurface,
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
                                    {.model        = OfflineMaterialPreviewModel::HairAbsorption,
                                     .firstTexture = spectrum(absorption.sigmaA)}
                                );
                            }
                            else
                            {
                                emit(
                                    {.model         = OfflineMaterialPreviewModel::HairPigments,
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

void prepareLights(
    OfflineSceneData&                 data,
    std::span<const DrawItem>         items,
    std::span<const OfflineLightData> lights,
    AssetManager&                     assets,
    RenderResourceManager&            resources,
    MaterialTable&                    materialTable
)
{
    std::unordered_map<MeshAsset::ID, MeshGeometry> geometries;
    CHECK(lights.size() < kOfflineInvalidIndex);
    for (const auto& source : lights)
    {
        OfflineLightEmission emission;
        emission.colorScale = {previewRgb(source.emission.spectrum), static_cast<float>(source.emission.scale)};
        if (source.emission.image)
        {
            const auto image = materialTable.textureReference(source.emission.image->image, resources);
            emission.resources.w |= kOfflineLightHasImage;
            emission.resources.x = image.textureIndex;
            emission.resources.y = image.samplerIndex;
        }
        if (source.emission.power)
        {
            emission.resources.w |= kOfflineLightHasPower;
            emission.normalization.x = static_cast<float>(*source.emission.power);
        }
        if (source.emission.illuminance)
        {
            emission.resources.w |= kOfflineLightHasIlluminance;
            emission.normalization.y = static_cast<float>(*source.emission.illuminance);
        }
        if (source.emission.medium)
        {
            const auto found     = std::ranges::find(data.lightMedia, *source.emission.medium);
            emission.resources.z = static_cast<uint32_t>(found - data.lightMedia.begin());
            if (found == data.lightMedia.end())
                data.lightMedia.push_back(*source.emission.medium);
        }

        std::visit(
            [&]<class T>(const T& model)
            {
                if constexpr (std::same_as<T, OfflineLightData::MeshArea>)
                {
                    const auto emissionIndex = static_cast<uint32_t>(data.areaEmissions.size());
                    auto&      draw          = data.draws[model.drawItemIndex];
                    DCHECK(draw.firstAreaLight == kOfflineInvalidIndex);
                    draw.firstAreaLight       = static_cast<uint32_t>(data.lightTriangles.size());
                    const auto& item          = items[model.drawItemIndex];
                    const auto& mesh          = assets.get(item.mesh);
                    auto [geometry, inserted] = geometries.try_emplace(item.mesh);
                    if (inserted)
                        geometry->second = context().assetDataManager->readGeometry(mesh);
                    const auto& vertices = geometry->second.vertices;
                    const auto& indices  = geometry->second.indices;
                    const auto& submesh  = mesh.submeshes[item.submeshIndex];
                    const bool  flipNormal =
                        (glm::determinant(glm::mat3{item.model}) < 0.0f) != model.reverseOrientation;
                    CHECK(data.lightTriangles.size() + submesh.indexCount / 3 < kOfflineInvalidIndex);
                    for (uint32_t offset = 0; offset < submesh.indexCount; offset += 3)
                    {
                        const auto&     v0 = vertices[indices[submesh.firstIndex + offset]];
                        const auto&     v1 = vertices[indices[submesh.firstIndex + offset + 1]];
                        const auto&     v2 = vertices[indices[submesh.firstIndex + offset + 2]];
                        const glm::vec3 p0{item.model * glm::vec4{v0.position, 1}};
                        const glm::vec3 p1{item.model * glm::vec4{v1.position, 1}};
                        const glm::vec3 p2{item.model * glm::vec4{v2.position, 1}};
                        const auto      cross  = glm::cross(p1 - p0, p2 - p0);
                        const auto      length = glm::length(cross);
                        const auto      normal =
                            length > 0.0f ? cross * ((flipNormal ? -1.0f : 1.0f) / length) : glm::vec3{0};
                        data.lightTriangles.push_back({
                            .p0Area        = {p0, length * 0.5f},
                            .p1            = {p1, 0},
                            .p2            = {p2, 0},
                            .normal        = {normal, 0},
                            .uv01          = {v0.texcoord, v1.texcoord},
                            .uv2           = v2.texcoord,
                            .emissionIndex = emissionIndex,
                            .twoSided      = model.twoSided ? 1u : 0u,
                        });
                        emission.normalization.z += length * 0.5f;
                    }
                    data.areaEmissions.push_back(emission);
                }
                else if constexpr (std::same_as<T, OfflineLightData::Point>)
                    data.pointLights.push_back({.position = model.position, .emission = emission});
                else if constexpr (std::same_as<T, OfflineLightData::Spot>)
                    data.spotLights.push_back(
                        {.position  = model.position,
                         .cosInner  = model.cosInner,
                         .direction = model.direction,
                         .cosOuter  = model.cosOuter,
                         .emission  = emission}
                    );
                else if constexpr (std::same_as<T, OfflineLightData::Directional>)
                    data.directionalLights.push_back({.direction = model.direction, .emission = emission});
                else if constexpr (std::same_as<T, OfflineLightData::Projection>)
                    data.projectionLights.push_back(
                        {.lightFromWorld = model.lightFromWorld,
                         .position       = model.position,
                         .fieldOfView    = model.fieldOfView,
                         .emission       = emission}
                    );
                else if constexpr (std::same_as<T, OfflineLightData::Goniometric>)
                    data.goniometricLights.push_back(
                        {.lightFromWorld = model.lightFromWorld, .position = model.position, .emission = emission}
                    );
                else if constexpr (std::same_as<T, OfflineLightData::Environment>)
                {
                    CHECK(!data.hasEnvironmentLight, "offline path tracing supports one environment light");
                    CHECK(data.lightPortals.size() + model.portal.size() < kOfflineInvalidIndex);
                    data.environmentLight = {
                        .lightFromWorld = model.lightFromWorld,
                        .firstPortal    = static_cast<uint32_t>(data.lightPortals.size()),
                        .portalCount    = static_cast<uint32_t>(model.portal.size()),
                        .emission       = emission
                    };
                    data.hasEnvironmentLight = true;
                    for (const auto vertex : model.portal)
                        data.lightPortals.emplace_back(vertex, 1);
                }
            },
            source.model
        );
    }
}
}

const OfflineSceneData& OfflineScene::prepare(
    std::span<const DrawItem>         items,
    std::span<const OfflineLightData> lights,
    AssetManager&                     assets,
    RenderResourceManager&            resources,
    MaterialTable&                    materialTable
)
{
    if (previousAssetRevision == assets.contentRevision() && std::ranges::equal(previousItems, items) &&
        std::ranges::equal(previousLights, lights))
        return data;
    const auto nextRevision = data.revision + 1;
    data                    = {};
    data.revision           = nextRevision;
    MaterialCompiler compiler(data, assets, resources, materialTable);
    for (const auto& item : items)
    {
        CHECK(item.offlineSurface, "offline surface data missing for mesh '{}'", item.mesh);
        data.draws.push_back({compiler.compile(item.materialId), item.offlineSurface->reverseOrientation ? 1u : 0u});
    }
    prepareLights(data, items, lights, assets, resources, materialTable);
    previousItems.assign(items.begin(), items.end());
    previousLights.assign(lights.begin(), lights.end());
    previousAssetRevision = assets.contentRevision();
    return data;
}
