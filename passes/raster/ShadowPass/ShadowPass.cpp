#include "passes/raster/ShadowPass/ShadowPass.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "core/Logger.h"
#include "core/math/Bounds.h"
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/raster/MeshDraw.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/ShaderData.h"
#include "render/resources/ShaderManager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
struct ShadowPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  lightIdx     = 0;
    uint32_t  cascadeIndex = 0;
    uint32_t  faceIndex    = 0;
    uint32_t  padding2     = 0;
};
static_assert(sizeof(ShadowPushConstants) == 80);
static_assert(offsetof(ShadowPushConstants, lightIdx) == 64);
static_assert(offsetof(ShadowPushConstants, cascadeIndex) == 68);

constexpr uint32_t kMinimumShadowLayers = 2;

void validateCascade(const ShadowSettings::Cascade& cascade)
{
    CHECK(cascade.count > 0 && cascade.count <= kMaxDirectionalShadowCascades);
    CHECK(cascade.shadowMapSize > 1);
    CHECK(std::isfinite(cascade.maxDistance) && cascade.maxDistance > 0.0f);
    CHECK(
        cascade.splitStrategy == CascadeSplitStrategy::Practical ||
        cascade.splitStrategy == CascadeSplitStrategy::Uniform
    );
    CHECK(std::isfinite(cascade.splitLambda) && cascade.splitLambda >= 0.0f && cascade.splitLambda <= 1.0f);
    CHECK(std::isfinite(cascade.blendRatio) && cascade.blendRatio >= 0.0f && cascade.blendRatio <= 0.5f);
    CHECK(std::isfinite(cascade.sampleRadius) && cascade.sampleRadius >= 0.0f);
    CHECK(cascade.shadowMapSize > 2.0f * (cascade.sampleRadius + 1.0f));
}

constexpr std::array kFaceDirections = {
    glm::vec3{1.0f, 0.0f, 0.0f},
    glm::vec3{-1.0f, 0.0f, 0.0f},
    glm::vec3{0.0f, 1.0f, 0.0f},
    glm::vec3{0.0f, -1.0f, 0.0f},
    glm::vec3{0.0f, 0.0f, 1.0f},
    glm::vec3{0.0f, 0.0f, -1.0f},
};

// Face up vectors account for Vulkan's downward-growing image coordinates.
constexpr std::array kFaceUp = {
    glm::vec3{0.0f, -1.0f, 0.0f},
    glm::vec3{0.0f, -1.0f, 0.0f},
    glm::vec3{0.0f, 0.0f, 1.0f},
    glm::vec3{0.0f, 0.0f, -1.0f},
    glm::vec3{0.0f, -1.0f, 0.0f},
    glm::vec3{0.0f, -1.0f, 0.0f},
};

using FrustumCorners = std::array<glm::vec3, 8>;

constexpr size_t kPlaneCornerCount         = FrustumCorners{}.size() / 2;
constexpr float  kShadowRadiusQuantization = 16.0f;  // Round outward to 1/16 meter.

struct FrustumSlice
{
    FrustumCorners corners;
    float          depthUpperBound = 0.0f;
    float          blendStartDepth = 0.0f;
};

struct BoundingSphere
{
    glm::vec3 center;
    float     radius;
};

BoundingSphere boundFrustumCorners(const FrustumCorners& corners)
{
    glm::vec3 center{0.0f};
    for (const auto& corner : corners)
        center += corner;
    center /= static_cast<float>(corners.size());

    float radiusSquared = 0.0f;
    for (const auto& corner : corners)
    {
        const auto offset = corner - center;
        radiusSquared     = std::max(radiusSquared, glm::dot(offset, offset));
    }

    return {center, std::sqrt(radiusSquared)};
}

FrustumCorners restoreFrustumCorners(const glm::mat4& inverseViewProjection)
{
    constexpr std::array kClipCorners = {
        glm::vec3{-1.0f, -1.0f, 0.0f},
        glm::vec3{1.0f, -1.0f, 0.0f},
        glm::vec3{1.0f, 1.0f, 0.0f},
        glm::vec3{-1.0f, 1.0f, 0.0f},
        glm::vec3{-1.0f, -1.0f, 1.0f},
        glm::vec3{1.0f, -1.0f, 1.0f},
        glm::vec3{1.0f, 1.0f, 1.0f},
        glm::vec3{-1.0f, 1.0f, 1.0f},
    };
    FrustumCorners frustumCorners;
    for (size_t index = 0; index < frustumCorners.size(); ++index)
    {
        const glm::vec4 world = inverseViewProjection * glm::vec4{kClipCorners[index], 1.0f};

        frustumCorners[index] = glm::vec3{world} / world.w;
    }
    return frustumCorners;
}

std::array<FrustumSlice, kMaxDirectionalShadowCascades>
splitFrustum(const FrustumCorners& corners, float nearPlane, float farPlane, const ShadowSettings::Cascade& config)
{
    std::array<FrustumSlice, kMaxDirectionalShadowCascades> slices{};
    float                                                   previousDepth  = nearPlane;
    float                                                   sliceNearDepth = nearPlane;
    for (uint32_t cascadeIndex = 0; cascadeIndex < config.count; ++cascadeIndex)
    {
        const float fraction     = static_cast<float>(cascadeIndex + 1) / config.count;
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * fraction;
        float       splitDepth   = uniformSplit;
        if (config.splitStrategy == CascadeSplitStrategy::Practical)
        {
            const float logarithmicSplit = nearPlane * std::pow(farPlane / nearPlane, fraction);
            splitDepth                   = std::lerp(uniformSplit, logarithmicSplit, config.splitLambda);
        }
        if (cascadeIndex + 1 == config.count)
            splitDepth = farPlane;
        const float nearFraction = (sliceNearDepth - nearPlane) / (farPlane - nearPlane);
        const float farFraction  = (splitDepth - nearPlane) / (farPlane - nearPlane);

        auto& slice = slices[cascadeIndex];
        for (size_t index = 0; index < kPlaneCornerCount; ++index)
        {
            slice.corners[index] = glm::mix(corners[index], corners[index + kPlaneCornerCount], nearFraction);
            slice.corners[index + kPlaneCornerCount] =
                glm::mix(corners[index], corners[index + kPlaneCornerCount], farFraction);
        }
        slice.depthUpperBound = splitDepth;
        slice.blendStartDepth = cascadeIndex + 1 < config.count
            ? splitDepth - (splitDepth - previousDepth) * config.blendRatio
            : splitDepth;
        // The next cascade must also render the current cascade's blend interval.
        sliceNearDepth = slice.blendStartDepth;
        previousDepth  = splitDepth;
    }
    return slices;
}

DirectionalShadowData
makeDirectionalShadow(const SceneUniforms& view, const LightData& light, const ShadowSettings::Cascade& config)
{
    CHECK(config.shadowMapSize > 1);

    // 1. Restore the camera frustum's world-space corners.
    auto frustumCorners = restoreFrustumCorners(view.inverseViewProjection);

    // 2. Truncate the far plane to the configured shadow distance.
    const float cameraNear = -(view.view * glm::vec4{frustumCorners.front(), 1.0f}).z;
    const float cameraFar  = -(view.view * glm::vec4{frustumCorners.back(), 1.0f}).z;
    CHECK(std::isfinite(cameraNear) && std::isfinite(cameraFar) && cameraNear > 0.0f && cameraFar > cameraNear);
    const float shadowFar = std::min(config.maxDistance, cameraFar);
    CHECK(
        shadowFar > cameraNear,
        "directional shadow distance {} must exceed camera near plane {}",
        config.maxDistance,
        cameraNear
    );
    const float farFraction = (shadowFar - cameraNear) / (cameraFar - cameraNear);
    for (size_t index = 0; index < kPlaneCornerCount; ++index)
        frustumCorners[index + kPlaneCornerCount] =
            glm::mix(frustumCorners[index], frustumCorners[index + kPlaneCornerCount], farFraction);

    // 3. Split the truncated frustum into cascade slices.
    const auto slices = splitFrustum(frustumCorners, cameraNear, shadowFar, config);

    // 4. Enclose each slice in a sphere, then fit its light-aligned bounding cube.
    const glm::vec3 lightDirection{light.direction};
    CHECK(std::isfinite(glm::dot(lightDirection, lightDirection)) && glm::dot(lightDirection, lightDirection) > 0.0f);
    const auto direction = glm::normalize(lightDirection);
    const auto up        = std::abs(direction.y) < 0.99f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.0f};
    const auto lightView = glm::lookAt(glm::vec3{0.0f}, direction, up);

    DirectionalShadowData shadow;
    for (uint32_t cascadeIndex = 0; cascadeIndex < config.count; ++cascadeIndex)
    {
        const auto& slice  = slices[cascadeIndex];
        const auto  sphere = boundFrustumCorners(slice.corners);
        glm::vec3   lightCenter{lightView * glm::vec4{sphere.center, 1.0f}};

        // Stabilize the grid scale against small floating-point changes in the sphere radius.
        const float quantizedRadius = std::ceil(sphere.radius * kShadowRadiusQuantization) / kShadowRadiusQuantization;
        const float mapSize         = static_cast<float>(config.shadowMapSize);
        // Reserve the PCF footprint, plus half a texel each for snapping and linear comparison filtering.
        const float guardTexels = config.sampleRadius + 1.0f;
        CHECK(mapSize > 2.0f * guardTexels);
        const float halfExtent         = quantizedRadius * mapSize / (mapSize - 2.0f * guardTexels);
        const float worldUnitsPerTexel = 2.0f * halfExtent / mapSize;
        lightCenter.x                  = std::round(lightCenter.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        lightCenter.y                  = std::round(lightCenter.y / worldUnitsPerTexel) * worldUnitsPerTexel;

        const glm::vec3 extent{halfExtent};
        const auto      min = lightCenter - extent;
        const auto      max = lightCenter + extent;

        // Light-view forward is -Z. Upstream casters are retained by depth clamping.
        glm::mat4 projection = glm::ortho(min.x, max.x, min.y, max.y, -max.z, -min.z);

        shadow.cascades[cascadeIndex] = {
            .vp              = projection * lightView,
            .depthUpperBound = slice.depthUpperBound,
            .blendStartDepth = slice.blendStartDepth,
        };
    }
    return shadow;
}

struct ShadowVolume
{
    uint32_t             mask;
    std::vector<Frustum> frusta;
};

[[nodiscard]] uint32_t shadowMask(GpuLightType type, uint32_t shadowIndex, uint32_t cascadeIndex = 0)
{
    constexpr uint32_t kPointOffset       = 0;
    constexpr uint32_t kSpotOffset        = kPointOffset + kMaxPointLightShadow;
    constexpr uint32_t kRectAreaOffset    = kSpotOffset + kMaxSpotLightShadow;
    constexpr uint32_t kDirectionalOffset = kRectAreaOffset + kMaxRectAreaLightShadow;
    constexpr uint32_t kMaskBitCount =
        kDirectionalOffset + kMaxDirectionalLightShadow * kMaxDirectionalShadowCascades;
    static_assert(kMaskBitCount <= 32);

    switch (type)
    {
        case GpuLightType::Point:
            return 1u << (kPointOffset + shadowIndex);
        case GpuLightType::Spot:
            return 1u << (kSpotOffset + shadowIndex);
        case GpuLightType::RectArea:
            return 1u << (kRectAreaOffset + shadowIndex);
        case GpuLightType::Directional:
            return 1u << (kDirectionalOffset + shadowIndex * kMaxDirectionalShadowCascades + cascadeIndex);
    }

    return 0;
}
}  // namespace

void ShadowSettings::validate() const
{
    validateCascade(cascade);
    CHECK(pointShadowMapSize > 0 && rectAreaShadowMapSize > 0 && spotShadowMapSize > 0);
    CHECK(std::isfinite(bias) && bias >= 0.0f);
    CHECK(std::isfinite(sampleRadius) && sampleRadius >= 0.0f);
    CHECK(std::isfinite(spotSampleRadius) && spotSampleRadius >= 0.0f);
    CHECK(filter == SamplerAsset::Filter::Nearest || filter == SamplerAsset::Filter::Linear);
}

ShadowPass::ShadowPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device)), parameters(deserializeFromProperties<ShadowSettings>(properties))
{
    parameters.validate();
}

Properties ShadowPass::getProperties() const { return serializeToProperties(parameters); }

void ShadowPass::setProperties(const Properties& properties)
{
    ShadowSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.enabled != parameters.enabled || next.cascade.count != parameters.cascade.count ||
        next.cascade.shadowMapSize != parameters.cascade.shadowMapSize ||
        next.pointShadowMapSize != parameters.pointShadowMapSize ||
        next.spotShadowMapSize != parameters.spotShadowMapSize ||
        next.rectAreaShadowMapSize != parameters.rectAreaShadowMapSize;
    parameters = std::move(next);
    if (recompile)
        requestRecompile();
}

RenderPassReflection ShadowPass::reflect(const CompileData&)
{
    constexpr auto kDepthSampled = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::ShaderResource;
    RenderPassReflection reflection;
    reflection.addOutput(kShadowUniformsPort, rhi::BufferDesc{
        .size = sizeof(ShadowUniforms),
        .usage = rhi::BufferUsage::ShaderResource,
    });

    const auto addOutputs =
        [&](const ShadowPorts& ports, uint32_t size, uint32_t layers, uint32_t lightCount, uint32_t recordSize)
    {
        const auto extent = parameters.enabled ? size : 1u;
        // Lighting shaders bind array views, including the single-cascade case.
        const auto arraySize = parameters.enabled ? std::max(layers, kMinimumShadowLayers) : kMinimumShadowLayers;
        reflection.addOutput(ports.map, rhi::TextureDesc{
            .type = rhi::TextureType::Texture2DArray,
            .size = {extent, extent, 1},
            .arrayLength = arraySize,
            .format = rhi::Format::D32Float,
            .usage = kDepthSampled,
        });
        reflection.addOutput(ports.data, rhi::BufferDesc{
            .size = (parameters.enabled ? lightCount : 1u) * recordSize,
            .usage = rhi::BufferUsage::ShaderResource,
        });
    };
    addOutputs(
        kPointShadowPorts, parameters.pointShadowMapSize, kMaxPointLightShadow * kPointFaceCount,
        kMaxPointLightShadow, sizeof(PointShadowData)
    );
    addOutputs(
        kSpotShadowPorts, parameters.spotShadowMapSize, kMaxSpotLightShadow, kMaxSpotLightShadow, sizeof(SpotShadowData)
    );
    addOutputs(
        kRectAreaShadowPorts, parameters.rectAreaShadowMapSize, kMaxRectAreaLightShadow,
        kMaxRectAreaLightShadow, sizeof(RectAreaShadowData)
    );
    addOutputs(
        kDirectionalShadowPorts, parameters.cascade.shadowMapSize, kMaxDirectionalLightShadow * parameters.cascade.count,
        kMaxDirectionalLightShadow, sizeof(DirectionalShadowData)
    );
    return reflection;
}

void ShadowPass::compile(RenderContext* passContext, const CompileData&)
{
    if (!parameters.enabled)
        return;

    const auto& shadow       = parameters;
    cascadeCount             = shadow.cascade.count;

    CHECK(cascadeCount > 0 && cascadeCount <= kMaxDirectionalShadowCascades);
    auto&       device = *passContext->device;
    const auto& limits = device.getInfo().limits;
    CHECK(
        shadow.cascade.shadowMapSize <= limits.maxTextureDimension2D &&
        kMaxDirectionalLightShadow * cascadeCount <= limits.maxTextureLayers
    );
    const auto createTarget = [&](GpuLightType shadowType, uint32_t viewLayerCount = 1)
    {
        const auto shader = passContext->shaderManager->getOrLoad(
            "shadow",
            {{"SHADOW_TYPE", std::to_string(static_cast<int>(shadowType))}}
        );

        const auto              input = createMeshInputLayout(device);
        rhi::RenderPipelineDesc desc;
        desc.program                    = shader;
        desc.inputLayout                = input;
        desc.depthStencil               = {.format = rhi::Format::D32Float, .depthTestEnable = true};
        desc.rasterizer.depthClipEnable = shadowType != GpuLightType::Directional;
        desc.rasterizer.scissorEnable   = true;
        rhi::ComPtr<rhi::IRenderPipeline> pipeline;
        rhiCheck(device.createRenderPipeline(desc, pipeline.writeRef()));


        return ShadowTarget{
            .pipeline       = pipeline,
            .viewLayerCount = viewLayerCount,
        };
    };

    point       = createTarget(GpuLightType::Point, kPointFaceCount);
    spot        = createTarget(GpuLightType::Spot);
    rectArea    = createTarget(GpuLightType::RectArea);
    directional = createTarget(GpuLightType::Directional);
}

void ShadowPass::prepareShadowData(
    const SceneUniforms& view, std::span<const LightData> frameLights, const ShadowSettings& shadows
)
{
    CHECK(shadows.cascade.count > 0 && shadows.cascade.count <= kMaxDirectionalShadowCascades);

    shadowData.pointShadows.clear();
    shadowData.spotShadows.clear();
    shadowData.rectAreaShadows.clear();
    shadowData.directionalShadows.clear();

    for (const auto& light : frameLights)
    {
        if (!light.castShadow())
            continue;

        if (light.type() == GpuLightType::Directional)
        {

            CHECK(
                light.shadowIdx < kMaxDirectionalLightShadow,
                "enabled shadow-casting directional lights exceed capacity {}",
                kMaxDirectionalLightShadow
            );
            shadowData.directionalShadows.push_back(makeDirectionalShadow(view, light, shadows.cascade));
            continue;
        }

        const float range = light.range();
        CHECK(std::isfinite(range) && range > 0.0f);
        const glm::vec3 position = light.position();

        if (light.type() == GpuLightType::Point)
        {

            CHECK(
                light.shadowIdx < kMaxPointLightShadow,
                "enabled shadow-casting point lights exceed capacity {}",
                kMaxPointLightShadow
            );

            const float      nearPlane  = std::min(0.1f, range * 0.1f);
            const glm::mat4  projection = glm::perspective(std::numbers::pi_v<float> * 0.5f, 1.0f, nearPlane, range);
            PointShadowData& shadow     = shadowData.pointShadows.emplace_back();
            for (size_t face = 0; face < shadow.vp.size(); ++face)
                shadow.vp[face] = projection * glm::lookAt(position, position + kFaceDirections[face], kFaceUp[face]);
        }
        else if (light.type() == GpuLightType::Spot)
        {

            CHECK(
                light.shadowIdx < kMaxSpotLightShadow,
                "enabled shadow-casting spot lights exceed capacity {}",
                kMaxSpotLightShadow
            );
            const float cosOuter = light.areaSizeCone.w;
            const float cosInner = light.areaSizeCone.z;
            CHECK(std::isfinite(cosOuter) && cosOuter > 0.0f && cosOuter < 1.0f);
            CHECK(std::isfinite(cosInner) && cosInner >= cosOuter && cosInner <= 1.0f);

            const glm::vec3 direction = glm::normalize(glm::vec3{light.direction});
            const glm::vec3 up        = glm::normalize(glm::cross(light.areaRight(), direction));
            CHECK(std::isfinite(glm::dot(direction, direction)) && std::isfinite(glm::dot(up, up)));
            const float nearPlane  = std::min(0.1f, range * 0.1f);
            glm::mat4   projection = glm::perspective(2.0f * std::acos(cosOuter), 1.0f, nearPlane, range);

            shadowData.spotShadows.push_back({.vp = projection * glm::lookAt(position, position + direction, up)});
        }
        else if (light.type() == GpuLightType::RectArea)
        {

            CHECK(
                light.shadowIdx < kMaxRectAreaLightShadow,
                "enabled shadow-casting rect area lights exceed capacity {}",
                kMaxRectAreaLightShadow
            );
            const glm::vec2 areaSize{light.areaSizeCone};
            CHECK(std::isfinite(areaSize.x) && std::isfinite(areaSize.y) && areaSize.x > 0.0f && areaSize.y > 0.0f);

            const glm::vec3 direction = glm::normalize(glm::vec3{light.direction});
            const glm::vec3 up        = glm::normalize(glm::cross(light.areaRight(), direction));
            const float     nearPlane = std::min(0.5f, range * 0.1f);
            glm::mat4 projection = glm::perspective(glm::radians(100.0f), areaSize.x / areaSize.y, nearPlane, range);

            shadowData.rectAreaShadows.push_back({
                .vp             = projection * glm::lookAt(position, position + direction, up),
                .nearPlane      = nearPlane,
                .farPlane       = range,
                .worldToUvScale = {0.5f * std::abs(projection[0][0]), 0.5f * std::abs(projection[1][1])},
            });
        }
        else
        {
            CHECK(0);
        }
    }
}

void ShadowPass::updateShadowData(const RenderContext& frame, const RenderData& renderData)
{
    prepareShadowData(frame.scene->cameraUniforms(), frame.scene->lights(), parameters);

    std::vector<ShadowVolume> volumes;
    for (uint32_t index = 0; index < shadowData.pointShadows.size(); ++index)
    {
        ShadowVolume volume{shadowMask(GpuLightType::Point, index), {}};
        for (const auto& vp : shadowData.pointShadows[index].vp)
            volume.frusta.emplace_back(vp);
        volumes.push_back(std::move(volume));
    }
    for (uint32_t index = 0; index < shadowData.spotShadows.size(); ++index)
        volumes.push_back({shadowMask(GpuLightType::Spot, index), {Frustum{shadowData.spotShadows[index].vp}}});
    for (uint32_t index = 0; index < shadowData.rectAreaShadows.size(); ++index)
        volumes.push_back(
            {shadowMask(GpuLightType::RectArea, index),
             {Frustum{shadowData.rectAreaShadows[index].vp}}}
        );
    // Cascades depth-clamp upstream casters; the far plane still bounds relevant geometry.
    for (uint32_t index = 0; index < shadowData.directionalShadows.size(); ++index)
        for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            volumes.push_back(
                {shadowMask(GpuLightType::Directional, index, cascade),
                 {Frustum{shadowData.directionalShadows[index].cascades[cascade].vp, Frustum::NearPlane::Exclude}}}
            );

    const auto draws = frame.scene->drawCalls();
    visibilityMasks.clear();
    visibilityMasks.reserve(draws.size());

    for (const auto& draw : draws)
    {
        uint32_t mask = 0;
        for (const auto& volume : volumes)
        {
            if (!std::ranges::any_of(volume.frusta, [&](const Frustum& frustum) { return frustum.intersects(draw.bounds); }))
                continue;

            mask |= volume.mask;
        }
        visibilityMasks.push_back(mask);
    }

    const auto upload = [&](const ShadowPorts& ports, const auto& values)
    {
        if (values.empty())
            return;
        const auto bytes = values.size() * sizeof(values.front());
        rhiCheck(frame.commands->uploadBufferData(renderData.getBuffer(ports.data), 0, bytes, values.data()));
    };
    upload(kPointShadowPorts, shadowData.pointShadows);
    upload(kSpotShadowPorts, shadowData.spotShadows);
    upload(kRectAreaShadowPorts, shadowData.rectAreaShadows);
    upload(kDirectionalShadowPorts, shadowData.directionalShadows);
}

void ShadowPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto& frame = *renderCommands;
    const ShadowUniforms uniforms{
        .cascadeCount            = parameters.cascade.count,
        .directionalSampleRadius = parameters.cascade.sampleRadius,
        .bias                    = parameters.bias,
        .sampleRadius            = parameters.sampleRadius,
        .spotSampleRadius        = parameters.spotSampleRadius,
        .pixelRotation           = uint32_t(parameters.enabled && parameters.pixelRotation),
        .enabled                 = uint32_t(parameters.enabled),
        .linearFilter            = uint32_t(parameters.filter == SamplerAsset::Filter::Linear),
    };
    rhiCheck(frame.commands->uploadBufferData(renderData.getBuffer(kShadowUniformsPort), 0, sizeof(uniforms), &uniforms));
    if (!parameters.enabled)
        return;

    updateShadowData(frame, renderData);
    renderShadowMaps(frame, renderData);
}

void ShadowPass::renderShadowMaps(
    const RenderContext& execution, const RenderData& renderData
)
{
    const auto  lights = execution.scene->lights();
    const auto& ranges = execution.scene->lightRanges();
    const auto attachments = [&](const ShadowPorts& ports)
    { return std::pair{renderData.getTexture(ports.map), renderData.getBuffer(ports.data)}; };

    const auto [cascadeMap, cascadeTransforms] = attachments(kDirectionalShadowPorts);
    for (uint32_t index = 0; index < ranges.directionalEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
            recordShadow(
                execution,
                directional,
                *cascadeMap,
                cascadeTransforms,
                index,
                light.shadowIdx * cascadeCount + cascadeIndex,
                cascadeIndex
            );
    }

    const auto [pointMap, pointTransforms] = attachments(kPointShadowPorts);
    for (uint32_t index = ranges.directionalEnd; index < ranges.pointEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        recordShadow(execution, point, *pointMap, pointTransforms, index, light.shadowIdx * kPointFaceCount);
    }

    const auto [rectAreaMap, rectAreaTransforms] = attachments(kRectAreaShadowPorts);
    for (uint32_t index = ranges.pointEnd; index < ranges.rectAreaEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        recordShadow(execution, rectArea, *rectAreaMap, rectAreaTransforms, index, light.shadowIdx);
    }

    const auto [spotMap, spotTransforms] = attachments(kSpotShadowPorts);
    for (uint32_t index = ranges.rectAreaEnd; index < ranges.spotEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        recordShadow(execution, spot, *spotMap, spotTransforms, index, light.shadowIdx);
    }
}

void ShadowPass::recordShadow(
    const RenderContext& execution,
    const ShadowTarget&          target,
    rhi::ITexture&               image,
    rhi::IBuffer*               transforms,
    uint32_t                     lightIndex,
    uint32_t                     baseArrayLayer,
    uint32_t                     cascadeIndex
) const
{
    // TODO: Precompute the per-layer attachment views.
    const auto& light      = execution.scene->lights()[lightIndex];
    const auto  visibility = shadowMask(light.type(), light.shadowIdx, cascadeIndex);
    for (uint32_t face = 0; face < target.viewLayerCount; ++face)
    {
        const auto view = image.createView(
            {.subresourceRange = {.layer = baseArrayLayer + face, .layerCount = 1, .mip = 0, .mipCount = 1}}
        );
        CHECK(view);
        rhi::RenderPassDepthStencilAttachment depth;
        depth.view = view;
        auto* pass = execution.commands->beginRenderPass({nullptr, 0, &depth});
        CHECK(pass);
        const auto& size = image.getDesc().size;
        auto state = ViewportRect{0, 0, float(size.width), float(size.height)}.renderState();
        const auto draws = execution.scene->drawCalls();
        for (size_t index = 0; index < draws.size(); ++index)
        {
            if (!(visibilityMasks[index] & visibility))
                continue;
            const auto& item   = draws[index];
            auto*       object = pass->bindPipeline(target.pipeline);
            CHECK(object);
            const rhi::ShaderCursor root(object);
            rhiCheck(root["lights"].setBinding(execution.scene->lightBuffer()));
            rhiCheck(root["shadowData"].setBinding(transforms));
            const ShadowPushConstants
                constants{.model = item.model, .lightIdx = lightIndex, .cascadeIndex = cascadeIndex, .faceIndex = face};
            rhiCheck(root["pushConstants"].getDereferenced().setData(constants));
            const auto& mesh        = *item.mesh;
            state.vertexBufferCount = 1;
            state.vertexBuffers[0]  = {mesh.vertices(), 0};
            state.indexBuffer       = {mesh.indices(), 0};
            pass->setRenderState(state);
            pass->drawIndexed({item.indexCount, 1, 0, 0, item.firstIndex});
        }
        pass->end();
    }
}
