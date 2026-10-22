#include "render/feature/shadow/ShadowData.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>
#include "core/Logger.h"

namespace
{

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

using FrustumCorners                       = std::array<glm::vec3, 8>;
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
    const FrustumCorners& corners, float nearPlane, float farPlane, const RenderFeature::Shadow::Cascade& config
)
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
makeDirectionalShadow(const SceneUniforms& view, const LightData& light, const RenderFeature::Shadow::Cascade& config)
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
}  // namespace


ShadowFrameData prepareShadows(
    const SceneUniforms& view, std::span<const LightData> frameLights, const RenderFeature::Shadow& shadows
)
{
    ShadowFrameData result;
    CHECK(shadows.cascade.count > 0 && shadows.cascade.count <= kMaxDirectionalShadowCascades);
    result.lights.assign(frameLights.begin(), frameLights.end());
    for (LightData& light : result.lights)
    {
        light.shadowidx = ~0u;
        if (!light.castShadow())
            continue;

        if (light.type() == GpuLightType::Directional)
        {
            light.shadowidx = static_cast<uint32_t>(result.directionalShadows.size());
            CHECK(
                light.shadowidx < kMaxDirectionalLightShadow,
                "enabled shadow-casting directional lights exceed capacity {}",
                kMaxDirectionalLightShadow
            );
            result.directionalShadows.push_back(makeDirectionalShadow(view, light, shadows.cascade));
            continue;
        }
        if (light.type() != GpuLightType::Point && light.type() != GpuLightType::RectArea &&
            light.type() != GpuLightType::Spot)
            continue;

        const float range = light.range();
        CHECK(std::isfinite(range) && range > 0.0f);
        const glm::vec3 position = light.position();

        if (light.type() == GpuLightType::Point)
        {
            light.shadowidx = static_cast<uint32_t>(result.pointShadows.size());
            CHECK(
                light.shadowidx < kMaxPointLightShadow,
                "enabled shadow-casting point lights exceed capacity {}",
                kMaxPointLightShadow
            );

            const float      nearPlane  = std::min(0.1f, range * 0.1f);
            const glm::mat4  projection = glm::perspective(std::numbers::pi_v<float> * 0.5f, 1.0f, nearPlane, range);
            PointShadowData& shadow     = result.pointShadows.emplace_back();
            for (size_t face = 0; face < shadow.vp.size(); ++face)
                shadow.vp[face] = projection * glm::lookAt(position, position + kFaceDirections[face], kFaceUp[face]);
        }
        else if (light.type() == GpuLightType::Spot)
        {
            light.shadowidx = static_cast<uint32_t>(result.spotShadows.size());
            CHECK(
                light.shadowidx < kMaxSpotLightShadow,
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
            result.spotShadows.push_back({.vp = projection * glm::lookAt(position, position + direction, up)});
        }
        else
        {
            light.shadowidx = static_cast<uint32_t>(result.rectAreaShadows.size());
            CHECK(
                light.shadowidx < kMaxRectAreaLightShadow,
                "enabled shadow-casting rect area lights exceed capacity {}",
                kMaxRectAreaLightShadow
            );
            const glm::vec2 areaSize{light.areaSizeCone};
            CHECK(std::isfinite(areaSize.x) && std::isfinite(areaSize.y) && areaSize.x > 0.0f && areaSize.y > 0.0f);

            const glm::vec3 direction = glm::normalize(glm::vec3{light.direction});
            const glm::vec3 up        = glm::normalize(glm::cross(light.areaRight(), direction));
            const float     nearPlane = std::min(0.5f, range * 0.1f);
            glm::mat4 projection = glm::perspective(glm::radians(100.0f), areaSize.x / areaSize.y, nearPlane, range);
            projection[1][1] *= -1.0f;
            result.rectAreaShadows.push_back({
                .vp             = projection * glm::lookAt(position, position + direction, up),
                .nearPlane      = nearPlane,
                .farPlane       = range,
                .worldToUvScale = {0.5f * std::abs(projection[0][0]), 0.5f * std::abs(projection[1][1])},
            });
        }
    }


    return result;
}
