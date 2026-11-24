#include "render/resources/SceneShading.h"

#include "asset/AssetDataManager.h"
#include "asset/pbrt/PbrtSpectrum.h"
#include "core/Logger.h"
#include "render/GpuScene.h"
#include "render/raytracing/RectLightGeometry.h"
#include "render/resources/MaterialTable.h"

#include <algorithm>
#include <array>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <unordered_map>

namespace
{
void appendMeshEmitter(
    SceneShadingData& data,
    std::unordered_map<MeshAsset::ID, MeshGeometry>& geometries,
    GpuDraw& draw,
    const AssetDataManager& assetData,
    GpuLightEmission emission,
    bool twoSided,
    bool reverseOrientation
)
{
    const auto& mesh = *draw.asset;
    auto [geometry, inserted] = geometries.try_emplace(mesh.id);
    if (inserted)
        geometry->second = assetData.readGeometry(mesh);

    const auto& vertices = geometry->second.vertices;
    const auto& indices = geometry->second.indices;
    CHECK(draw.indexCount % 3 == 0 && uint64_t(draw.firstIndex) + draw.indexCount <= indices.size(),
        "invalid emissive submesh index range in mesh '{}'", mesh.id);
    const bool flipNormal = draw.mirroredPbrtSurface != reverseOrientation;
    const auto emissionIndex = static_cast<uint32_t>(data.areaEmissions.size());
    CHECK(data.lightTriangles.size() + draw.indexCount / 3 < kInvalidLightIndex);
    draw.firstAreaLight = static_cast<uint32_t>(data.lightTriangles.size());

    for (uint32_t offset = 0; offset < draw.indexCount; offset += 3)
    {
        const auto& v0 = vertices[indices[draw.firstIndex + offset]];
        const auto& v1 = vertices[indices[draw.firstIndex + offset + 1]];
        const auto& v2 = vertices[indices[draw.firstIndex + offset + 2]];
        const glm::vec3 p0{draw.model * glm::vec4{v0.position, 1}};
        const glm::vec3 p1{draw.model * glm::vec4{v1.position, 1}};
        const glm::vec3 p2{draw.model * glm::vec4{v2.position, 1}};
        const auto cross = glm::cross(p1 - p0, p2 - p0);
        const auto length = glm::length(cross);
        const auto normal = length > 0.0f ? cross * ((flipNormal ? -1.0f : 1.0f) / length) : glm::vec3{0};
        data.lightTriangles.push_back({
            .p0Area = {p0, length * 0.5f},
            .p1 = {p1, 0},
            .p2 = {p2, 0},
            .normal = {normal, 0},
            .uv01 = {v0.texcoord, v1.texcoord},
            .uv2 = v2.texcoord,
            .emissionIndex = emissionIndex,
            .twoSided = twoSided ? 1u : 0u,
        });
        emission.normalization.z += length * 0.5f;
    }
    data.areaEmissions.push_back(emission);
}
}  // namespace

void SceneShading::prepareLights(
    std::span<const SceneLight> lights, const AssetDataManager& assetData, MaterialTable& materials
)
{
    auto& data = shadingData;
    meshEmitters.clear();
    std::array<std::vector<LightData>, 4> analyticGroups;
    CHECK(lights.size() < kInvalidLightIndex);
    for (const auto& source : lights)
    {
        GpuLightEmission emission;
        emission.colorScale = {pbrtPreviewRgb(source.emission.spectrum, assetData), static_cast<float>(source.emission.scale)};
        if (source.emission.image)
        {
            const auto image = materials.textureReference(source.emission.image->image);
            emission.resources.w |= kLightHasImage;
            emission.resources.x = image.textureIndex;
            emission.resources.y = image.samplerIndex;
        }
        const glm::vec3 color = glm::vec3{emission.colorScale} * emission.colorScale.w;
        const auto appendAnalytic = [&](LightData light)
        {
            auto& group = analyticGroups[static_cast<size_t>(light.type())];
            light.color = color;
            light.shadowEnabled = source.castShadow ? 1u : 0u;
            group.push_back(light);
        };
        if (source.emission.power)
        {
            emission.resources.w |= kLightHasPower;
            emission.normalization.x = static_cast<float>(*source.emission.power);
        }
        if (source.emission.illuminance)
        {
            emission.resources.w |= kLightHasIlluminance;
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
                if constexpr (std::same_as<T, SceneLight::MeshArea>)
                {
                    emission.resources.w |= kLightFlipUv;
                    meshEmitters.push_back({model, emission});
                }
                else if constexpr (std::same_as<T, SceneLight::Point>)
                    appendAnalytic({.positionRange = {model.position, model.range}, .lightType = GpuLightType::Point});
                else if constexpr (std::same_as<T, SceneLight::Spot>)
                    appendAnalytic({
                        .spotFalloffExponent = model.falloffExponent,
                        .positionRange = {model.position, model.range},
                        .direction = {model.direction, 0},
                        .areaSizeCone = {0, 0, model.cosInner, model.cosOuter},
                        .lightType = GpuLightType::Spot,
                        .areaRightData = {model.right, 0},
                    });
                else if constexpr (std::same_as<T, SceneLight::Directional>)
                    appendAnalytic({.direction = {model.direction, 0}, .lightType = GpuLightType::Directional});
                else if constexpr (std::same_as<T, SceneLight::Rectangle>)
                    appendAnalytic({
                        .positionRange = {model.position, model.range},
                        .direction = {model.direction, 0},
                        .areaSizeCone = {model.size, 0, 0},
                        .lightType = GpuLightType::RectArea,
                        .areaRightData = {model.right, 0},
                    });
                else if constexpr (std::same_as<T, SceneLight::Projection>)
                    data.projectionLights.push_back(
                        {.lightFromWorld = model.lightFromWorld,
                         .position       = model.position,
                         .fieldOfView    = model.fieldOfView,
                         .emission       = emission}
                    );
                else if constexpr (std::same_as<T, SceneLight::Goniometric>)
                    data.goniometricLights.push_back(
                        {.lightFromWorld = model.lightFromWorld, .position = model.position, .emission = emission}
                    );
                else if constexpr (std::same_as<T, SceneLight::Environment>)
                {
                    CHECK(!data.hasEnvironmentLight, "scene has more than one environment light component");
                    CHECK(data.lightPortals.size() + model.portal.size() < kInvalidLightIndex);
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

    const auto appendGroup = [&](GpuLightType type)
    {
        uint32_t shadowIndex = 0;
        for (auto& light : analyticGroups[static_cast<size_t>(type)])
        {
            light.shadowIdx = light.castShadow() ? shadowIndex++ : ~0u;
            data.lights.push_back(light);
        }
        return static_cast<uint32_t>(data.lights.size());
    };
    data.lightRanges = {
        appendGroup(GpuLightType::Directional),
        appendGroup(GpuLightType::Point),
        appendGroup(GpuLightType::RectArea),
        appendGroup(GpuLightType::Spot),
    };
    CHECK(data.lights.size() <= kMaxLights, "scene has {} analytic lights; maximum is {}", data.lights.size(), kMaxLights);
}

void SceneShading::buildEmissiveTriangles(
    std::span<GpuDraw> draws, const AssetDataManager& assetData,
    const MaterialTable& materials
)
{
    auto& data = shadingData;
    std::unordered_map<MeshAsset::ID, MeshGeometry> geometries;
    for (const auto& emitter : meshEmitters)
    {
        const auto& mesh = emitter.mesh;
        appendMeshEmitter(
            data,
            geometries,
            draws[mesh.drawItemIndex],
            assetData,
            emitter.emission,
            mesh.twoSided,
            mesh.reverseOrientation
        );
    }

    for (auto& draw : draws)
    {
        if (draw.firstAreaLight != kInvalidLightIndex || !materials.info(draw.materialIndex).emissive)
            continue;

        const auto& material = materials.data(draw.materialIndex);
        const GpuLightEmission emission{
            .colorScale = {glm::vec3{material.emissiveFactor}, 1},
            .resources = {
                material.emissiveTexture.textureIndex,
                material.emissiveTexture.samplerIndex,
                kInvalidLightIndex,
                kLightHasImage | kLightMultiplyColor,
            },
        };
        appendMeshEmitter(
            data, geometries, draw, assetData, emission, material.doubleSided != 0, draw.reverseOrientation
        );
    }

    data.firstRectTriangle = static_cast<uint32_t>(data.lightTriangles.size());
    const auto rectCount = data.lightRanges.rectAreaEnd - data.lightRanges.pointEnd;
    CHECK(data.lightTriangles.size() + uint64_t(rectCount) * 2 < kInvalidLightIndex);
    for (uint32_t index = data.lightRanges.pointEnd; index < data.lightRanges.rectAreaEnd; ++index)
    {
        const auto& light = data.lights[index];
        const auto model = rectLightTransform(light);
        const float triangleArea = 0.5f * light.areaSizeCone.x * light.areaSizeCone.y;
        const auto emissionIndex = static_cast<uint32_t>(data.areaEmissions.size());
        data.areaEmissions.push_back({.colorScale = {light.color, 1}});

        for (size_t offset = 0; offset < kRectLightIndices.size(); offset += 3)
        {
            const auto vertex = [&](size_t corner)
            {
                return model * glm::vec4{kRectLightVertices[kRectLightIndices[offset + corner]], 1};
            };
            data.lightTriangles.push_back({
                .p0Area = {glm::vec3{vertex(0)}, triangleArea},
                .p1 = vertex(1),
                .p2 = vertex(2),
                .normal = light.direction,
                .emissionIndex = emissionIndex,
            });
        }
    }
}

void SceneShading::update(
    std::span<const SceneLight> lights,
    const AssetDataManager& assetData,
    MaterialTable& materials
)
{
    auto& data = shadingData;
    const auto nextRevision = data.revision + 1;
    data                    = {};
    data.revision           = nextRevision;

    prepareLights(lights, assetData, materials);
}
