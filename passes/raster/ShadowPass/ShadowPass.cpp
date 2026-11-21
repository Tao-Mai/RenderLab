#include "render/device/ResourceBarriers.h"
#include "passes/raster/ShadowPass/ShadowPass.h"

#include "core/profiling/Profiler.h"

#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/FrameData.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/resources/ShaderManager.h"
#include "render/Visibility.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr char kPointLightShadowMap[] = "pointLightShadowMap";
constexpr char kSpotLightShadowMap[] = "spotLightShadowMap";
constexpr char kRectAreaLightShadowMap[] = "rectAreaLightShadowMap";
constexpr char kCascadeShadowMap[] = "cascadeShadowMap";
constexpr char kPointShadowBuffer[] = "pointShadowBuffer";
constexpr char kSpotShadowBuffer[] = "spotShadowBuffer";
constexpr char kRectAreaShadowBuffer[] = "rectAreaShadowBuffer";
constexpr char kDirectionalShadowBuffer[] = "directionalShadowBuffer";

void validateCascade(const ShadowSettings::Cascade& cascade)
{
    CHECK(cascade.count > 0 && cascade.count <= kMaxDirectionalShadowCascades);
    CHECK(cascade.shadowMapSize > 1);
    CHECK(std::isfinite(cascade.maxDistance) && cascade.maxDistance > 0.0f);
    CHECK(cascade.splitStrategy == CascadeSplitStrategy::Practical ||
          cascade.splitStrategy == CascadeSplitStrategy::Uniform);
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
    DCHECK(radiusSquared > 0.0f);
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
        DCHECK(world.w != 0.0f);
        frustumCorners[index] = glm::vec3{world} / world.w;
    }
    return frustumCorners;
}

std::array<FrustumSlice, kMaxDirectionalShadowCascades> splitFrustum(
    const FrustumCorners& corners, float nearPlane, float farPlane, const ShadowSettings::Cascade& config
)
{
    std::array<FrustumSlice, kMaxDirectionalShadowCascades> slices{};
    float                                               previousDepth  = nearPlane;
    float                                               sliceNearDepth = nearPlane;
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
        projection[1][1] *= -1.0f;
        projection[3][1] *= -1.0f;
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
    uint32_t             visibleDrawItems = 0;
};
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

ShadowPass::ShadowPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<ShadowSettings>(properties))
{
    parameters.validate();
}

Falcor::Properties ShadowPass::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void ShadowPass::setProperties(const Falcor::Properties& properties)
{
    ShadowSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile =
        next.enabled != parameters.enabled || next.cascade.count != parameters.cascade.count ||
        next.cascade.shadowMapSize != parameters.cascade.shadowMapSize ||
        next.pointShadowMapSize != parameters.pointShadowMapSize ||
        next.spotShadowMapSize != parameters.spotShadowMapSize ||
        next.rectAreaShadowMapSize != parameters.rectAreaShadowMapSize;
    parameters = std::move(next);
    if (recompile)
        requestRecompile();
}

Falcor::RenderPassReflection ShadowPass::reflect(const CompileData&)
{
    constexpr auto kDepthSampled = Falcor::ResourceBindFlags::DepthStencil | Falcor::ResourceBindFlags::ShaderResource;
    Falcor::RenderPassReflection reflection;
    const auto pointSize = parameters.enabled ? parameters.pointShadowMapSize : 1u;
    const auto pointLayers = parameters.enabled ? kMaxPointLightShadow : 1u;
    reflection.addOutput(kPointLightShadowMap, "Point light shadow cube array")
        .format(Falcor::ResourceFormat::D32Float)
        .bindFlags(kDepthSampled)
        .textureCube(pointSize, pointSize, 1, pointLayers);

    if (!parameters.enabled)
    {
        reflection.addOutput(kPointShadowBuffer, "Point light shadow transforms")
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource)
            .rawBuffer(std::max({
                sizeof(PointShadowData),
                sizeof(SpotShadowData),
                sizeof(RectAreaShadowData),
                sizeof(DirectionalShadowData),
            }));
        return reflection;
    }

    const auto addMap = [&](const std::string& name, uint32_t size, uint32_t layers)
    {
        reflection.addOutput(name, "Shadow depth array")
            .format(Falcor::ResourceFormat::D32Float)
            .bindFlags(kDepthSampled)
            .texture2D(size, size, 1, 1, layers);
    };
    addMap(kSpotLightShadowMap, parameters.spotShadowMapSize, kMaxSpotLightShadow);
    addMap(kRectAreaLightShadowMap, parameters.rectAreaShadowMapSize, kMaxRectAreaLightShadow);
    addMap(kCascadeShadowMap, parameters.cascade.shadowMapSize, kMaxDirectionalLightShadow * parameters.cascade.count);

    reflection.addOutput(kPointShadowBuffer, "Point light shadow transforms")
        .bindFlags(Falcor::ResourceBindFlags::ShaderResource)
        .rawBuffer(kMaxPointLightShadow * sizeof(PointShadowData));
    reflection.addOutput(kSpotShadowBuffer, "Spot light shadow transforms")
        .bindFlags(Falcor::ResourceBindFlags::ShaderResource)
        .rawBuffer(kMaxSpotLightShadow * sizeof(SpotShadowData));
    reflection.addOutput(kRectAreaShadowBuffer, "Rectangular light shadow transforms")
        .bindFlags(Falcor::ResourceBindFlags::ShaderResource)
        .rawBuffer(kMaxRectAreaLightShadow * sizeof(RectAreaShadowData));
    reflection.addOutput(kDirectionalShadowBuffer, "Directional light cascade data")
        .bindFlags(Falcor::ResourceBindFlags::ShaderResource)
        .rawBuffer(kMaxDirectionalLightShadow * sizeof(DirectionalShadowData));
    return reflection;
}

void ShadowPass::compile(Falcor::RenderContext* passContext, const CompileData&)
{
    targetsBound = false;
    shadowAccesses.clear();
    barriers.clear();
    lightingAccesses.clear();
    for (auto& frame : passContext->frames)
        frame.uploadShadowUniforms({.enabled = 0u});

    if (!parameters.enabled)
        return;

    const auto& shadow = parameters;
    cascadeCount       = shadow.cascade.count;
    auto& resources = *passContext->resources;
    linearComparisonSampler  = resources.sampler(BuiltinAssets::Sampler::kShadowLinear);
    nearestComparisonSampler = resources.sampler(BuiltinAssets::Sampler::kShadowNearest);

    CHECK(cascadeCount > 0 && cascadeCount <= kMaxDirectionalShadowCascades);
    const auto& limits = passContext->vulkan->properties().limits;
    CHECK(
        shadow.cascade.shadowMapSize <= limits.maxImageDimension2D &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferWidth &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferHeight &&
        kMaxDirectionalLightShadow * cascadeCount <= limits.maxImageArrayLayers
    );
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features =
        passContext->vulkan->physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures;
    CHECK((features & kRequiredFeatures) == kRequiredFeatures);

    // pipeline layout
    auto&            device = *passContext->getDevice();
    const std::array descriptorSetLayouts{&device.sceneDescriptorSetLayout, &device.lightDescriptorSetLayout,
        &device.materialDescriptorSetLayout, &device.shadowDescriptorSetLayout};
    const std::array pushConstantRanges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(ShadowPushConstants),
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(descriptorSetLayouts, pushConstantRanges);

    const auto createTarget = [&](uint32_t mapSize,
                                  GpuLightType                shadowType,
                                  uint32_t                    viewMask       = 0,
                                  uint32_t                    viewLayerCount = 1)
    {
        const auto shader =
            passContext->shaderManager->getOrLoad("shadow", {{"SHADOW_TYPE", std::to_string(static_cast<int>(shadowType))}});
        const auto pipeline = device.getOrCreatePipeline({
            .shader = shader,
            .layout = pipelineLayout,
            .state =
                PipelineState{
                    .vertexLayout = VertexLayoutPreset::PositionOnly,
                    .depthClamp   = shadowType == GpuLightType::Directional,
                },
            .depthFormat = vk::Format::eD32Sfloat,
            .viewMask    = viewMask,
        });

        return ShadowTarget{
            .pipeline       = pipeline,
            .mapSize        = mapSize,
            .viewMask       = viewMask,
            .viewLayerCount = viewLayerCount,
        };
    };

    point = createTarget(shadow.pointShadowMapSize, GpuLightType::Point, kPointViewMask, kPointFaceCount);
    spot = createTarget(shadow.spotShadowMapSize, GpuLightType::Spot);
    rectArea = createTarget(shadow.rectAreaShadowMapSize, GpuLightType::RectArea);
    directional = createTarget(shadow.cascade.shadowMapSize, GpuLightType::Directional);
}

void ShadowPass::prepareTargets(VulkanResources& cache, const Falcor::RenderContext& frame, const Falcor::RenderData& renderData)
{
    const auto pointMap = cache.registerImage(renderData.getTexture(kPointLightShadowMap));
    const auto pointBuffer = cache.registerBuffer(renderData[kPointShadowBuffer]->asBuffer());
    ShadowResources outputs{
        .pointLightShadowMap     = pointMap,
        .spotLightShadowMap      = pointMap,
        .rectAreaLightShadowMap  = pointMap,
        .cascadeShadowMap        = pointMap,
        .pointShadowBuffer       = pointBuffer,
        .spotShadowBuffer        = pointBuffer,
        .rectAreaShadowBuffer    = pointBuffer,
        .directionalShadowBuffer = pointBuffer,
    };
    if (parameters.enabled)
    {
        outputs.spotLightShadowMap = cache.registerImage(renderData.getTexture(kSpotLightShadowMap));
        outputs.rectAreaLightShadowMap = cache.registerImage(renderData.getTexture(kRectAreaLightShadowMap));
        outputs.cascadeShadowMap = cache.registerImage(renderData.getTexture(kCascadeShadowMap));
        outputs.spotShadowBuffer = cache.registerBuffer(renderData[kSpotShadowBuffer]->asBuffer());
        outputs.rectAreaShadowBuffer = cache.registerBuffer(renderData[kRectAreaShadowBuffer]->asBuffer());
        outputs.directionalShadowBuffer = cache.registerBuffer(renderData[kDirectionalShadowBuffer]->asBuffer());
    }

    bindShadowResources(cache, outputs, frame.frames, *frame.resources);

    constexpr auto kFragment = vk::PipelineStageFlagBits2::eFragmentShader;
    for (const auto image : {outputs.pointLightShadowMap, outputs.spotLightShadowMap,
                             outputs.rectAreaLightShadowMap, outputs.cascadeShadowMap})
        lightingAccesses.add(image, vulkan_access::sampled(kFragment));
    for (const auto buffer : {outputs.pointShadowBuffer, outputs.spotShadowBuffer,
                              outputs.rectAreaShadowBuffer, outputs.directionalShadowBuffer})
        lightingAccesses.add(buffer, vulkan_access::storageRead(kFragment));
    lightingAccesses.declare(cache);
    targetsBound = true;

    if (!parameters.enabled)
        return;

    point.image = outputs.pointLightShadowMap;
    point.buffer = outputs.pointShadowBuffer;
    spot.image = outputs.spotLightShadowMap;
    spot.buffer = outputs.spotShadowBuffer;
    rectArea.image = outputs.rectAreaLightShadowMap;
    rectArea.buffer = outputs.rectAreaShadowBuffer;
    directional.image = outputs.cascadeShadowMap;
    directional.buffer = outputs.directionalShadowBuffer;

    constexpr auto kShaderStages = vk::PipelineStageFlagBits2::eVertexShader | kFragment;
    barriers.add(frame.frameData->lightBufferHandle(), vulkan_access::storageRead(kShaderStages));
    barriers.add(frame.materialTable->bufferHandle(), vulkan_access::storageRead(kFragment));
    for (const auto* target : {&point, &spot, &rectArea, &directional})
    {
        barriers.add(target->image, vulkan_access::kDepthWrite);
        shadowAccesses.add(target->buffer, vulkan_access::storageRead(kShaderStages));
    }
    barriers.declare(cache);
    shadowAccesses.declare(cache);
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
            DCHECK(light.shadowIdx == shadowData.directionalShadows.size());
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
            DCHECK(light.shadowIdx == shadowData.pointShadows.size());
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
            DCHECK(light.shadowIdx == shadowData.spotShadows.size());
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
            projection[1][1] *= -1.0f;
            shadowData.spotShadows.push_back({.vp = projection * glm::lookAt(position, position + direction, up)});
        }
        else if (light.type() == GpuLightType::RectArea)
        {
            DCHECK(light.shadowIdx == shadowData.rectAreaShadows.size());
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
            glm::mat4       projection = glm::perspective(glm::radians(100.0f), areaSize.x / areaSize.y, nearPlane, range);
            projection[1][1] *= -1.0f;
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

void ShadowPass::updateShadowData(VulkanResources& cache, const Falcor::RenderContext& frame)
{
    frame.frameData->uploadShadowUniforms({
        .cascadeCount          = parameters.cascade.count,
        .directionalSampleRadius = parameters.cascade.sampleRadius,
        .bias                  = parameters.bias,
        .sampleRadius          = parameters.sampleRadius,
        .spotSampleRadius      = parameters.spotSampleRadius,
        .pixelRotation         = parameters.pixelRotation ? 1u : 0u,
        .enabled               = 1u,
    });
    frame.frameData->writeShadowSampler(
        parameters.filter == SamplerAsset::Filter::Linear ? linearComparisonSampler : nearestComparisonSampler
    );

    CpuProfileScope timing{context().profiler, "Shadow preparation"};
    prepareShadowData(frame.view->uniforms, frame.scene->lights, parameters);

    std::vector<ShadowVolume> volumes;
    for (uint32_t index = 0; index < shadowData.pointShadows.size(); ++index)
    {
        ShadowVolume volume{RenderVisibility::shadowMask(GpuLightType::Point, index), {}};
        for (const auto& vp : shadowData.pointShadows[index].vp)
            volume.frusta.emplace_back(vp);
        volumes.push_back(std::move(volume));
    }
    for (uint32_t index = 0; index < shadowData.spotShadows.size(); ++index)
        volumes.push_back(
            {RenderVisibility::shadowMask(GpuLightType::Spot, index), {Frustum{shadowData.spotShadows[index].vp}}}
        );
    for (uint32_t index = 0; index < shadowData.rectAreaShadows.size(); ++index)
        volumes.push_back(
            {RenderVisibility::shadowMask(GpuLightType::RectArea, index), {Frustum{shadowData.rectAreaShadows[index].vp}}}
        );
    // Cascades depth-clamp upstream casters; the far plane still bounds relevant geometry.
    for (uint32_t index = 0; index < shadowData.directionalShadows.size(); ++index)
        for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            volumes.push_back(
                {RenderVisibility::shadowMask(GpuLightType::Directional, index, cascade),
                 {Frustum{shadowData.directionalShadows[index].cascades[cascade].vp, Frustum::NearPlane::Exclude}}}
            );

    visibilityMasks.clear();
    visibilityMasks.reserve(frame.scene->drawBounds.size());

    for (const auto& bounds : frame.scene->drawBounds)
    {
        uint32_t mask = 0;
        for (auto& volume : volumes)
        {
            if (!std::ranges::any_of(volume.frusta, [&](const Frustum& frustum) { return frustum.intersects(bounds); }))
                continue;

            mask |= volume.mask;
            ++volume.visibleDrawItems;
        }
        visibilityMasks.push_back(mask);
    }

    if (auto* profiler = context().profiler)
    {
        std::vector<ViewCullingStatistics> views;
        for (const auto& volume : volumes)
            views.push_back({RenderVisibility::viewName(volume.mask), volume.visibleDrawItems});
        profiler->recordCulling(static_cast<uint32_t>(frame.scene->drawBounds.size()), std::move(views));
    }

    const auto upload = [&](VulkanResources::BufferHandle buffer, const auto& values)
    {
        if (values.empty())
            return;
        const auto bytes      = values.size() * sizeof(values.front());
        const auto allocation = cache.allocateUpload(buffer, bytes);
        std::memcpy(allocation.data.data(), values.data(), bytes);
    };
    upload(point.buffer, shadowData.pointShadows);
    upload(spot.buffer, shadowData.spotShadows);
    upload(rectArea.buffer, shadowData.rectAreaShadows);
    upload(directional.buffer, shadowData.directionalShadows);
}

void ShadowPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commandBuffer = *renderCommands->commands;
    const auto& execution     = *renderCommands;

    if (!targetsBound)
        prepareTargets(cache, execution, renderData);
    if (parameters.enabled)
        renderShadowMaps(cache, commandBuffer, execution);

    lightingAccesses.transition(cache, commandBuffer);
}

void ShadowPass::renderShadowMaps(
    VulkanResources& cache, vk::raii::CommandBuffer& commandBuffer, const Falcor::RenderContext& execution
)
{
    barriers.transition(cache, commandBuffer);
    updateShadowData(cache, execution);
    cache.recordUploads(commandBuffer);
    shadowAccesses.transition(cache, commandBuffer);

    const auto& lights = execution.scene->lights;
    const auto& ranges = execution.scene->lightRanges;

    for (uint32_t index = 0; index < ranges.directionalEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowIdx < shadowData.directionalShadows.size());
        for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
            recordShadow(
                cache,
                commandBuffer,
                execution,
                directional,
                index,
                light.shadowIdx * cascadeCount + cascadeIndex,
                cascadeIndex
            );
    }

    for (uint32_t index = ranges.directionalEnd; index < ranges.pointEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowIdx < shadowData.pointShadows.size());
        recordShadow(cache, commandBuffer, execution, point, index, light.shadowIdx * kPointFaceCount);
    }

    for (uint32_t index = ranges.pointEnd; index < ranges.rectAreaEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowIdx < shadowData.rectAreaShadows.size());
        recordShadow(cache, commandBuffer, execution, rectArea, index, light.shadowIdx);
    }

    for (uint32_t index = ranges.rectAreaEnd; index < ranges.spotEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowIdx < shadowData.spotShadows.size());
        recordShadow(cache, commandBuffer, execution, spot, index, light.shadowIdx);
    }
}

void ShadowPass::recordShadow(
    VulkanResources&           cache,
    vk::raii::CommandBuffer& commandBuffer,
    const Falcor::RenderContext&         execution,
    const ShadowTarget&      target,
    uint32_t                 lightIndex,
    uint32_t                 baseArrayLayer,
    uint32_t                 cascadeIndex
) const
{
    // TODO 提前算
    const auto imageView =
        cache.getImage(target.image)
            .getOrCreateImageView({
                .viewType         = target.viewLayerCount > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D,
                .format           = vk::Format::eD32Sfloat,
                .subresourceRange = {
                    .aspectMask     = vk::ImageAspectFlagBits::eDepth,
                    .levelCount     = 1,
                    .baseArrayLayer = baseArrayLayer,
                    .layerCount     = target.viewLayerCount,
                },
            });
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = imageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
    };

    commandBuffer.beginRendering({
        .renderArea       = {{0, 0}, {target.mapSize, target.mapSize}},
        .layerCount       = 1,
        .viewMask         = target.viewMask,
        .pDepthAttachment = &depthAttachment,
    });

    // Multiview resets graphics state at the beginning of each rendering instance.
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, target.pipeline);
    const std::array lightSets = {execution.frameData->lightSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kLightSet,
        lightSets,
        {}
    );
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipelineLayout, RenderInterface::kShadowSet,
        execution.frameData->shadowSetHandle(), {}
    );
    commandBuffer.setViewport(
        0,
        vk::Viewport(0.0f, 0.0f, static_cast<float>(target.mapSize), static_cast<float>(target.mapSize), 0.0f, 1.0f)
    );
    commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {target.mapSize, target.mapSize}));

    const auto& light      = execution.scene->lights[lightIndex];
    const auto  visibility = RenderVisibility::shadowMask(light.type(), light.shadowIdx, cascadeIndex);
    for (size_t index = 0; index < execution.scene->drawItems.size(); ++index)
    {
        if (!(visibilityMasks[index] & visibility))
            continue;

        const auto&               item = execution.scene->drawItems[index];
        const ShadowPushConstants push{.model = item.model, .lightIdx = lightIndex, .cascadeIndex = cascadeIndex};
        commandBuffer.pushConstants<ShadowPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            push
        );
        auto&       mesh    = execution.resources->mesh(item.mesh);
        const auto& submesh = mesh.submeshes()[item.submeshIndex];
        mesh.bind(commandBuffer);
        commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        recordDraw(context().profiler, submesh.indexCount);
    }

    commandBuffer.endRendering();
}
