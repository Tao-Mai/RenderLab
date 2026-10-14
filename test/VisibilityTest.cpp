#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/SceneAsset.h"
#include "core/Context.h"
#include "core/Serializer.h"
#include "core/math/Bounds.h"
#include "render/RenderPreparation.h"
#include "render/Visibility.h"
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

TEST(Visibility, DirectionalCascadesFitTranslatedFrustumAndBoundsPadding)
{
    constexpr float kCameraNear = 0.25f;
    constexpr float kCameraFar  = 80.0f;
    constexpr float kFov        = glm::radians(60.0f);
    constexpr float kAspect     = 1.5f;
    constexpr float kTolerance  = 1e-4f;
    ViewUniforms    view;
    view.view                  = glm::lookAt(glm::vec3{17, 8, 23}, glm::vec3{12, 4, -1}, glm::vec3{0, 1, 0});
    view.viewProjection        = glm::perspective(kFov, kAspect, kCameraNear, kCameraFar) * view.view;
    view.inverseViewProjection = glm::inverse(view.viewProjection);

    RenderFeature::Shadow config;
    config.cascade = {.count = 4, .maxDistance = 12.0f, .padding = 0.01f};
    LightData  light;
    const auto direction = glm::normalize(glm::vec3{0.4f, -1.0f, -0.3f});
    light.direction      = glm::vec4{direction, 0.0f};
    light.flags          = {static_cast<uint32_t>(GpuLightType::Directional), 1, 1, 0};
    const auto shadows   = prepareShadows(view, {&light, 1}, config);
    ASSERT_EQ(shadows.directionalShadows.size(), 1u);

    const auto inverseView   = glm::inverse(view.view);
    float      previousDepth = kCameraNear;
    for (const auto& cascade : shadows.directionalShadows.front().cascades)
    {
        ASSERT_GT(cascade.depthUpperBound, previousDepth);
        for (float depth : {previousDepth, cascade.depthUpperBound})
            for (float x : {-1.0f, 1.0f})
                for (float y : {-1.0f, 1.0f})
                {
                    const float     halfHeight = depth * std::tan(kFov * 0.5f);
                    const glm::vec3 corner{
                        inverseView * glm::vec4{x * halfHeight * kAspect, y * halfHeight, -depth, 1}
                    };
                    for (float padding : {-config.cascade.padding, 0.0f, config.cascade.padding})
                    {
                        const auto clip = cascade.vp * glm::vec4{corner + direction * padding, 1};
                        EXPECT_LE(std::abs(clip.x), clip.w + kTolerance);
                        EXPECT_LE(std::abs(clip.y), clip.w + kTolerance);
                        EXPECT_GE(clip.z, -kTolerance);
                        EXPECT_LE(clip.z, clip.w + kTolerance);
                    }
                }
        previousDepth = cascade.depthUpperBound;
    }
    EXPECT_NEAR(previousDepth, config.cascade.maxDistance, kTolerance);
}

TEST(Visibility, DirectionalShadowRetainsUpstreamCasters)
{
    AssetManager    assets;
    SceneRenderData scene;
    for (const auto position : {glm::vec3{0, 100, -1}, glm::vec3{0, -100, -1}, glm::vec3{100, 100, -1}})
        scene.objects.push_back({
            .mesh  = BuiltinAssets::Mesh::cube,
            .model = glm::translate(glm::mat4{1}, position),
        });
    LightData light;
    light.direction = {0, -1, 0, 0};
    light.flags     = {static_cast<uint32_t>(GpuLightType::Directional), 1, 1, 0};
    scene.lights.push_back(light);

    ViewUniforms view;
    view.viewProjection        = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
    view.inverseViewProjection = glm::inverse(view.viewProjection);
    RenderFeature::Shadow shadows;
    shadows.cascade     = {.count = 1, .maxDistance = 10.0f};
    const auto prepared = prepareRenderScene(scene, view, shadows, assets, nullptr);
    const auto mask     = RenderVisibility::shadowMask(GpuLightType::Directional, 0, 0);
    ASSERT_EQ(prepared.objects.size(), 3u);
    EXPECT_EQ(prepared.objects[0].visibilityMask, mask);
    EXPECT_FALSE(prepared.objects[1].visibilityMask & mask);
    EXPECT_FALSE(prepared.objects[2].visibilityMask & mask);
}

TEST(Visibility, MainAndShadowBitsAreIndependentAndDisabledLightsHaveNoBit)
{
    AssetManager assets;
    SceneAsset   scene;
    auto         object                             = std::make_unique<AStaticMesh>();
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

    ViewUniforms view;
    view.viewProjection        = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
    view.inverseViewProjection = glm::inverse(view.viewProjection);
    RenderSystem system;
    const auto&  snapshot = system.prepare(scene);
    ASSERT_EQ(snapshot.objects.size(), 1u);  // Never remove geometry needed by RT.
    ASSERT_EQ(snapshot.lights.size(), 4u);
    const auto prepare = [&](const RenderFeature::Shadow& shadows = RenderFeature::Shadow{})
    { return prepareRenderScene(system.prepare(scene), view, shadows, assets, nullptr); };
    const auto mask = prepare().objects.front().visibilityMask;
    EXPECT_FALSE(mask & RenderVisibility::kMainView);
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::Point, 0));
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::Spot, 0));
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::RectArea, 0));
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::Directional, 0, 0));

    scene.actors.front()->transform().position.z             = -3;
    scene.actors[1]->getComponent<LightComponent>()->enabled = false;
    const auto visible                                       = prepare().objects.front().visibilityMask;
    EXPECT_TRUE(visible & RenderVisibility::kMainView);
    EXPECT_FALSE(visible & RenderVisibility::shadowMask(GpuLightType::Point, 0));
    EXPECT_FALSE(visible & RenderVisibility::shadowMask(GpuLightType::Spot, 0));
    EXPECT_FALSE(visible & RenderVisibility::shadowMask(GpuLightType::RectArea, 0));
    EXPECT_EQ(prepare({.enable = false}).objects.front().visibilityMask, RenderVisibility::kMainView);

    scene.actors.front()->transform().position = {1000, 0, -3};
    EXPECT_EQ(prepare().objects.front().visibilityMask, 0u);
    EXPECT_EQ(system.prepare(scene).objects.size(), 1u);

    const auto& builtin = assets.get<MeshAsset>(BuiltinAssets::Mesh::cube);
    MeshAsset   restored;
    Deserialize(Serialize(builtin), restored);
    EXPECT_EQ(restored.bounds, builtin.bounds);
    system.reset();
}
