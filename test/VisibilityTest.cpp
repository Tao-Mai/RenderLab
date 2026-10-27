#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/SceneAsset.h"
#include "core/Context.h"
#include "core/Serializer.h"
#include "core/math/Bounds.h"
#include "render/Visibility.h"
#include "render/raster/ShadowData.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/system/RenderSystem.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

TEST(Visibility, TransformsBoundsWithRotationAndNegativeScale)
{
    const Aabb local{{-1, -2, -3}, {1, 2, 3}};
    const auto model = glm::translate(glm::mat4{1}, glm::vec3{4, 5, 6}) *
        glm::rotate(glm::mat4{1}, glm::radians(90.0f), glm::vec3{0, 0, 1}) *
        glm::scale(glm::mat4{1}, glm::vec3{-2, 1, 0.5f});
    const auto world = local.transformed(model);
    EXPECT_NEAR(world.min.x, 2.0f, 1e-5f);
    EXPECT_NEAR(world.max.x, 6.0f, 1e-5f);
    EXPECT_NEAR(world.min.y, 3.0f, 1e-5f);
    EXPECT_NEAR(world.max.y, 7.0f, 1e-5f);
    EXPECT_FLOAT_EQ(world.min.z, 4.5f);
    EXPECT_FLOAT_EQ(world.max.z, 7.5f);
}

TEST(Visibility, UsesVulkanNearPlaneAndKeepsIntersectingBounds)
{
    const Frustum frustum{glm::mat4{1}};
    EXPECT_TRUE(frustum.intersects({{-0.5f, -0.5f, 0.2f}, {0.5f, 0.5f, 0.8f}}));
    EXPECT_TRUE(frustum.intersects({{0.9f, 0, 0}, {1.5f, 0.5f, 1}}));
    EXPECT_FALSE(frustum.intersects({{1.1f, 0, 0}, {2, 1, 1}}));
    EXPECT_FALSE(frustum.intersects({{-0.5f, -0.5f, -1}, {0.5f, 0.5f, -0.1f}}));
    EXPECT_FALSE(frustum.intersects({{-0.5f, -0.5f, 1.1f}, {0.5f, 0.5f, 2}}));
}

TEST(Visibility, ExcludingNearPlaneStillCullsAgainstOtherFivePlanes)
{
    const Frustum frustum{glm::mat4{1}, Frustum::NearPlane::Exclude};
    EXPECT_TRUE(frustum.intersects({{-0.5f, -0.5f, -100}, {0.5f, 0.5f, -99}}));
    EXPECT_FALSE(frustum.intersects({{-2, -0.5f, -100}, {-1.1f, 0.5f, -99}}));
    EXPECT_FALSE(frustum.intersects({{1.1f, -0.5f, -100}, {2, 0.5f, -99}}));
    EXPECT_FALSE(frustum.intersects({{-0.5f, -2, -100}, {0.5f, -1.1f, -99}}));
    EXPECT_FALSE(frustum.intersects({{-0.5f, 1.1f, -100}, {0.5f, 2, -99}}));
    EXPECT_FALSE(frustum.intersects({{-0.5f, -0.5f, 1.1f}, {0.5f, 0.5f, 2}}));
}

TEST(Visibility, DirectionalCascadeCubesContainBlendedFrustum)
{
    constexpr float kCameraNear = 0.25f;
    constexpr float kCameraFar  = 80.0f;
    constexpr float kFov        = glm::radians(60.0f);
    constexpr float kAspect     = 1.5f;
    constexpr float kTolerance  = 1e-4f;
    SceneUniforms   view;
    view.view                  = glm::lookAt(glm::vec3{17, 8, 23}, glm::vec3{12, 4, -1}, glm::vec3{0, 1, 0});
    view.viewProjection        = glm::perspective(kFov, kAspect, kCameraNear, kCameraFar) * view.view;
    view.inverseViewProjection = glm::inverse(view.viewProjection);

    RenderFeature::Shadow config;
    config.cascade = {.count = 4, .maxDistance = 12.0f};
    RenderLight light;
    const auto  direction    = glm::normalize(glm::vec3{0.4f, -1.0f, -0.3f});
    light.data.direction     = glm::vec4{direction, 0.0f};
    light.data.lightType     = GpuLightType::Directional;
    light.data.shadowEnabled = 1;
    light.data.shadowIdx     = 0;
    const auto shadows       = prepareShadows(view, {&light.data, 1}, config);
    ASSERT_EQ(shadows.directionalShadows.size(), 1u);

    const auto inverseView    = glm::inverse(view.view);
    float      previousDepth  = kCameraNear;
    float      sliceNearDepth = kCameraNear;
    for (const auto& cascade : shadows.directionalShadows.front().cascades)
    {
        ASSERT_GT(cascade.depthUpperBound, previousDepth);
        EXPECT_GE(cascade.blendStartDepth, previousDepth);
        EXPECT_LE(cascade.blendStartDepth, cascade.depthUpperBound);
        const auto  inverseVp  = glm::inverse(cascade.vp);
        const float width      = 2.0f * glm::length(glm::vec3{inverseVp[0]});
        const float height     = 2.0f * glm::length(glm::vec3{inverseVp[1]});
        const float depthRange = glm::length(glm::vec3{inverseVp[2]});
        EXPECT_NEAR(width, height, kTolerance);
        EXPECT_NEAR(width, depthRange, kTolerance);
        for (float depth : {sliceNearDepth, cascade.depthUpperBound})
            for (float x : {-1.0f, 1.0f})
                for (float y : {-1.0f, 1.0f})
                {
                    const float     halfHeight = depth * std::tan(kFov * 0.5f);
                    const glm::vec3 corner{
                        inverseView * glm::vec4{x * halfHeight * kAspect, y * halfHeight, -depth, 1}
                    };
                    const auto clip = cascade.vp * glm::vec4{corner, 1};
                    EXPECT_LE(std::abs(clip.x), clip.w + kTolerance);
                    EXPECT_LE(std::abs(clip.y), clip.w + kTolerance);
                    EXPECT_GE(clip.z, -kTolerance);
                    EXPECT_LE(clip.z, clip.w + kTolerance);
                }
        sliceNearDepth = cascade.blendStartDepth;
        previousDepth  = cascade.depthUpperBound;
    }
    EXPECT_NEAR(previousDepth, config.cascade.maxDistance, kTolerance);
    EXPECT_FLOAT_EQ(shadows.directionalShadows[0].cascades.back().blendStartDepth, previousDepth);
}

TEST(Visibility, DirectionalShadowRetainsUpstreamCasters)
{
    AssetManager    assets;
    SceneRenderData scene;
    for (const auto position : {glm::vec3{0, 100, -1}, glm::vec3{0, -100, -1}, glm::vec3{100, 100, -1}})
        scene.drawItems.push_back({
            .mesh  = BuiltinAssets::Mesh::cube,
            .model = glm::translate(glm::mat4{1}, position),
        });
    RenderLight light;
    light.data.direction     = {0, -1, 0, 0};
    light.data.lightType     = GpuLightType::Directional;
    light.data.shadowEnabled = 1;
    light.data.shadowIdx     = 0;
    scene.lights.push_back(light);

    SceneUniforms view;
    view.viewProjection        = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
    view.inverseViewProjection = glm::inverse(view.viewProjection);
    RenderFeature::Shadow shadows;
    shadows.cascade        = {.count = 1, .maxDistance = 10.0f};
    const auto    lighting = prepareShadows(view, {&light.data, 1}, shadows);
    const Frustum frustum{lighting.directionalShadows[0].cascades[0].vp, Frustum::NearPlane::Exclude};
    const auto&   bounds = assets.get<MeshAsset>(BuiltinAssets::Mesh::cube).bounds;
    EXPECT_TRUE(frustum.intersects(bounds.transformed(scene.drawItems[0].model)));
    EXPECT_FALSE(frustum.intersects(bounds.transformed(scene.drawItems[1].model)));
    EXPECT_FALSE(frustum.intersects(bounds.transformed(scene.drawItems[2].model)));
}

TEST(Visibility, CascadeTexelSnappingKeepsSubTexelMotionStable)
{
    const auto  projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 40.0f);
    RenderLight light;
    light.data.direction     = {0, 0, -1, 0};
    light.data.lightType     = GpuLightType::Directional;
    light.data.shadowEnabled = 1;
    light.data.shadowIdx     = 0;
    RenderFeature::Shadow config;
    config.cascade     = {.count = 1, .shadowMapSize = 256, .maxDistance = 20.0f, .sampleRadius = 0.0f};
    const auto prepare = [&](float cameraX)
    {
        SceneUniforms view;
        view.view                  = glm::translate(glm::mat4{1}, glm::vec3{-cameraX, 0, 0});
        view.viewProjection        = projection * view.view;
        view.inverseViewProjection = glm::inverse(view.viewProjection);
        return prepareShadows(view, {&light.data, 1}, config).directionalShadows[0].cascades[0].vp;
    };
    const auto  reference          = prepare(0.0f);
    const float worldUnitsPerTexel = 2.0f / (reference[0][0] * config.cascade.shadowMapSize);
    for (float motion : {0.25f, 0.75f, -0.75f})
    {
        const auto  vp                = prepare(worldUnitsPerTexel * motion);
        const float expectedClipShift = -std::round(motion) * 2.0f / config.cascade.shadowMapSize;
        EXPECT_NEAR(vp[0][0], reference[0][0], 1e-6f);
        EXPECT_NEAR(vp[1][1], reference[1][1], 1e-6f);
        EXPECT_NEAR(vp[3][0] - reference[3][0], expectedClipShift, 1e-6f);
        EXPECT_NEAR(vp[3][1], reference[3][1], 1e-6f);
        const float halfHeight = 20.0f * std::tan(glm::radians(30.0f));
        for (float x : {-1.0f, 1.0f})
            for (float y : {-1.0f, 1.0f})
            {
                const auto clip = vp * glm::vec4{x * halfHeight + worldUnitsPerTexel * motion, y * halfHeight, -20, 1};
                EXPECT_LE(std::abs(clip.x), clip.w + 1e-5f);
                EXPECT_LE(std::abs(clip.y), clip.w + 1e-5f);
                EXPECT_GE(clip.z, -1e-5f);
                EXPECT_LE(clip.z, clip.w + 1e-5f);
            }
    }
    config.cascade.sampleRadius = 4.0f;
    EXPECT_LT(prepare(0.0f)[0][0], reference[0][0]);  // The filter footprint is included in the culling volume.
}

TEST(Visibility, UniformCascadeSplitsIgnoreLambdaAndPracticalKeepsNearResolution)
{
    SceneUniforms view;
    view.viewProjection        = glm::perspective(glm::radians(55.0f), 1.0f, 0.1f, 120.0f);
    view.inverseViewProjection = glm::inverse(view.viewProjection);
    RenderLight light;
    light.data.direction     = {0, -1, 0, 0};
    light.data.lightType     = GpuLightType::Directional;
    light.data.shadowEnabled = 1;
    light.data.shadowIdx     = 0;
    RenderFeature::Shadow config;
    const auto practical                  = prepareShadows(view, {&light.data, 1}, config);
    config.cascade.splitStrategy          = CascadeSplitStrategy::Uniform;
    const auto uniform                    = prepareShadows(view, {&light.data, 1}, config);
    config.cascade.splitLambda            = 1.0f;
    const auto uniformWithDifferentLambda = prepareShadows(view, {&light.data, 1}, config);
    for (uint32_t index = 0; index < config.cascade.count; ++index)
    {
        const float expected = 0.1f + (50.0f - 0.1f) * (index + 1) / config.cascade.count;
        EXPECT_NEAR(uniform.directionalShadows[0].cascades[index].depthUpperBound, expected, 1e-4f);
    }
    EXPECT_EQ(uniform.directionalShadows, uniformWithDifferentLambda.directionalShadows);
    EXPECT_LT(
        practical.directionalShadows[0].cascades[0].depthUpperBound,
        uniform.directionalShadows[0].cascades[0].depthUpperBound
    );
    EXPECT_LT(
        uniform.directionalShadows[0].cascades[0].blendStartDepth,
        uniform.directionalShadows[0].cascades[0].depthUpperBound
    );
    config.cascade.blendRatio  = 0.0f;
    const auto withoutBlending = prepareShadows(view, {&light.data, 1}, config);
    for (const auto& cascade : withoutBlending.directionalShadows[0].cascades)
        EXPECT_FLOAT_EQ(cascade.blendStartDepth, cascade.depthUpperBound);
}

TEST(Visibility, SceneSubmissionRetainsOffscreenObjectsAndDisabledLights)
{
    AssetManager assets;
    context().assetManager = &assets;
    SceneAsset scene;
    auto       object                               = std::make_unique<AStaticMesh>();
    object->getComponent<RenderComponent>()->meshId = BuiltinAssets::Mesh::cube;
    object->transform().position                    = {0, 0, 3};  // Behind camera, in front of the test lights.
    object->init();
    scene.actors.push_back(std::move(object));
    auto point                                        = std::make_unique<ALight>();
    point->getComponent<LightComponent>()->castShadow = true;
    point->init();
    scene.actors.push_back(std::move(point));
    auto spot                                        = std::make_unique<ALight>(std::make_unique<SpotLightComponent>());
    spot->transform().rotation                       = glm::quat(glm::vec3{0, glm::radians(180.0f), 0});
    spot->getComponent<LightComponent>()->castShadow = true;
    spot->init();
    scene.actors.push_back(std::move(spot));
    auto area                  = std::make_unique<ALight>(std::make_unique<RectAreaLightComponent>());
    area->transform().rotation = scene.actors.back()->transform().rotation;
    area->getComponent<LightComponent>()->castShadow = true;
    area->init();
    scene.actors.push_back(std::move(area));
    auto sun = std::make_unique<ALight>(std::make_unique<DirectionalLightComponent>());
    sun->getComponent<LightComponent>()->castShadow = true;
    sun->init();
    scene.actors.push_back(std::move(sun));

    SceneUniforms view;
    view.viewProjection        = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
    view.inverseViewProjection = glm::inverse(view.viewProjection);
    RenderSystem system;
    const auto&  snapshot = system.prepare(scene);
    const auto&  builtin  = assets.get<MeshAsset>(BuiltinAssets::Mesh::cube);
    ASSERT_EQ(snapshot.drawItems.size(), builtin.submeshes.size());  // Never remove geometry needed by RT.
    ASSERT_EQ(snapshot.lights.size(), 4u);
    scene.actors.front()->transform().position.z             = -3;
    scene.actors[1]->getComponent<LightComponent>()->enabled = false;
    const auto& disabledSnapshot                             = system.prepare(scene);
    ASSERT_EQ(disabledSnapshot.lights.size(), 4u);  // Scene submission still includes disabled components.
    EXPECT_FALSE(disabledSnapshot.lights[0].enabled);
    scene.actors.front()->transform().position = {1000, 0, -3};
    EXPECT_EQ(system.prepare(scene).drawItems.size(), builtin.submeshes.size());

    MeshAsset restored;
    Deserialize(Serialize(builtin), restored);
    EXPECT_EQ(restored.bounds, builtin.bounds);
    system.reset();
    context().assetManager = nullptr;
}
