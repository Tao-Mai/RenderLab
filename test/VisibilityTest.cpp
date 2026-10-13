#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/SceneAsset.h"
#include "core/Context.h"
#include "core/Serializer.h"
#include "core/math/Bounds.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/system/RenderSystem.h"
#include "scene/system/Visibility.h"

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

TEST(Visibility, MainAndShadowBitsAreIndependentAndDisabledLightsHaveNoBit)
{
    AssetManager assets;
    auto*        previousAssets = context().assetManager;
    context().assetManager      = &assets;
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

    ViewUniforms view;
    view.viewProjection        = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 20.0f);
    view.inverseViewProjection = glm::inverse(view.viewProjection);
    RenderSystem system;
    const auto&  snapshot = system.prepare(scene, view, {});
    ASSERT_EQ(snapshot.objects.size(), 1u);  // Never remove geometry needed by RT.
    const auto mask = snapshot.objects.front().visibilityMask;
    EXPECT_FALSE(mask & RenderVisibility::kMainView);
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::Point, 0));
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::Spot, 0));
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::RectArea, 0));
    EXPECT_TRUE(mask & RenderVisibility::shadowMask(GpuLightType::Directional, 0, 0));

    scene.actors.front()->transform().position.z             = -3;
    scene.actors[1]->getComponent<LightComponent>()->enabled = false;
    const auto visible = system.prepare(scene, view, {}).objects.front().visibilityMask;
    EXPECT_TRUE(visible & RenderVisibility::kMainView);
    EXPECT_FALSE(visible & RenderVisibility::shadowMask(GpuLightType::Point, 0));
    EXPECT_FALSE(visible & RenderVisibility::shadowMask(GpuLightType::Spot, 0));
    EXPECT_FALSE(visible & RenderVisibility::shadowMask(GpuLightType::RectArea, 0));
    EXPECT_EQ(
        system.prepare(scene, view, {.enable = false}).objects.front().visibilityMask,
        RenderVisibility::kMainView
    );

    scene.actors.front()->transform().position = {1000, 0, -3};
    EXPECT_EQ(system.prepare(scene, view, {}).objects.front().visibilityMask, 0u);

    const auto& builtin = assets.get<MeshAsset>(BuiltinAssets::Mesh::cube);
    MeshAsset   restored;
    Deserialize(Serialize(builtin), restored);
    EXPECT_EQ(restored.bounds, builtin.bounds);
    system.reset();
    context().assetManager = previousAssets;
}
