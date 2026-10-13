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

DirectionalShadowData
makeDirectionalShadow(const ViewUniforms& view, const LightData& light, const RenderFeature::Shadow::Cascade& config)
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
    constexpr size_t                           kPlaneCornerCount = kClipCorners.size() / 2;
    std::array<glm::vec3, kClipCorners.size()> frustumCorners;
    for (size_t index = 0; index < frustumCorners.size(); ++index)
    {
        const glm::vec4 world = view.inverseViewProjection * glm::vec4{kClipCorners[index], 1.0f};
        DCHECK(world.w != 0.0f);
        frustumCorners[index] = glm::vec3{world} / world.w;
    }

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

    const glm::vec3 lightDirection{light.direction};
    CHECK(std::isfinite(glm::dot(lightDirection, lightDirection)) && glm::dot(lightDirection, lightDirection) > 0.0f);
    const auto direction = glm::normalize(lightDirection);
    const auto up        = std::abs(direction.y) < 0.99f ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.0f};

    DirectionalShadowData shadow;
    float                 previousDepth = cameraNear;
    for (uint32_t cascadeIndex = 0; cascadeIndex < config.count; ++cascadeIndex)
    {
        const float fraction         = static_cast<float>(cascadeIndex + 1) / config.count;
        const float uniformSplit     = cameraNear + (shadowFar - cameraNear) * fraction;
        const float logarithmicSplit = cameraNear * std::pow(shadowFar / cameraNear, fraction);
        const float splitDepth       = cascadeIndex + 1 == config.count
                  ? shadowFar
                  : std::lerp(uniformSplit, logarithmicSplit, config.splitLambda);
        const float nearFraction     = (previousDepth - cameraNear) / (cameraFar - cameraNear);
        const float farFraction      = (splitDepth - cameraNear) / (cameraFar - cameraNear);

        std::array<glm::vec3, kClipCorners.size()> corners;
        glm::vec3                                  center{0.0f};
        for (size_t index = 0; index < kPlaneCornerCount; ++index)
        {
            corners[index] = glm::mix(frustumCorners[index], frustumCorners[index + kPlaneCornerCount], nearFraction);
            corners[index + kPlaneCornerCount] =
                glm::mix(frustumCorners[index], frustumCorners[index + kPlaneCornerCount], farFraction);
            center += corners[index] + corners[index + kPlaneCornerCount];
        }
        center /= static_cast<float>(corners.size());
        float radius = 0.0f;
        for (const auto& corner : corners)
            radius = std::max(radius, glm::length(corner - center));
        DCHECK(radius > 0.0f);

        // Extra light-view depth includes casters upstream of the visible camera slice.
        const float lightDistance = radius + config.depthPadding;
        const auto  lightView     = glm::lookAt(center - direction * lightDistance, center, up);
        glm::mat4   projection    = glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * lightDistance);
        projection[1][1] *= -1.0f;
        shadow.cascades[cascadeIndex] = {.vp = projection * lightView, .depthUpperBound = splitDepth};
        previousDepth                 = splitDepth;
    }
    return shadow;
}
}  // namespace


ShadowFrameData
prepareShadows(const ViewUniforms& view, std::span<const LightData> frameLights, const RenderFeature::Shadow& shadows)
{
    ShadowFrameData result;
    CHECK(shadows.cascade.count > 0 && shadows.cascade.count <= kMaxDirectionalShadowCascades);
    result.lights.assign(frameLights.begin(), frameLights.end());
    for (LightData& light : result.lights)
    {
        light.shadowidx = ~0u;
        if (!shadows.enable)
            light.flags.z = 0;
        if (!light.enable() || !light.castShadow())
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
