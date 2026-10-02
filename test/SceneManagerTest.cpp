#include "ecs/SceneManager.h"

#include "asset/JsonIo.h"
#include "ecs/component/CameraComponent.h"
#include "ecs/component/CharacterMoveComponent.h"
#include "ecs/component/LightComponent.h"
#include "ecs/component/FreeFlyMoveComponent.h"
#include "ecs/component/RenderComponent.h"
#include "ecs/component/TransformComponent.h"

#include <gtest/gtest.h>

namespace
{
Scene::Desc makeScene()
{
    Scene::Desc scene;
    scene.id = Scene::ID{"ecs-test"};
    scene.objects.push_back({
        .name = std::string{SceneManager::editorCameraName},
        .components = {
            {"Transform", ecs::TransformComponent{}}, {"Camera", ecs::CameraComponent{}}, {"FreeFlyMove", ecs::FreeFlyMoveComponent{}},
        },
    });
    scene.objects.push_back({
        .name = "Mesh and Light",
        .components = {
            {"Transform", ecs::TransformComponent{}}, {"Render", ecs::RenderComponent{}}, {"Light", ecs::LightComponent{}},
        },
    });
    return scene;
}
}

TEST(SceneManagerTest, ObjectsAndEntitiesShareTheSameComponents)
{
    SceneManager manager;
    manager.load(makeScene());

    ASSERT_EQ(manager.entities().size(), manager.scene().objects.size());
    EXPECT_TRUE((manager.registry().all_of<ecs::TransformComponent, ecs::CameraComponent, ecs::FreeFlyMoveComponent>(manager.editorCamera())));
    EXPECT_FALSE(manager.registry().all_of<ecs::RenderComponent>(manager.editorCamera()));

    auto& object = manager.scene().objects[1];
    auto& transform = manager.registry().get<ecs::TransformComponent>(manager.entities()[1]);
    EXPECT_EQ(object.components.at("Transform").try_cast<ecs::TransformComponent>(), &transform);

    transform.position.x = 12.0f;
    EXPECT_EQ(object.components.at("Transform").cast<ecs::TransformComponent&>().position.x, 12.0f);
    object.components.at("Light").cast<ecs::LightComponent&>().intensity = 4.0f;
    EXPECT_EQ(manager.registry().get<ecs::LightComponent>(manager.entities()[1]).intensity, 4.0f);
}

TEST(SceneManagerTest, PickingUsesOneIdForEachObjectAndRejectsStaleIds)
{
    SceneManager manager;
    manager.load(makeScene());

    const auto entity = manager.entities()[1];
    const auto id = SceneManager::selectionId(entity);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(manager.findObject(id), &manager.scene().objects[1]);
    EXPECT_EQ(manager.findObject(0), nullptr);

    manager.load(makeScene());
    EXPECT_FALSE(manager.registry().valid(entity));
    EXPECT_EQ(manager.findObject(id), nullptr);
}

TEST(SceneManagerTest, LoadingDetachesComponentsAndCanReloadItsOwnScene)
{
    const auto source = makeScene();
    SceneManager manager;
    manager.load(source);
    manager.registry().get<ecs::TransformComponent>(manager.editorCamera()).position.y = 7.0f;
    EXPECT_EQ(source.objects[0].components.at("Transform").cast<const ecs::TransformComponent&>().position.y, 0.0f);

    manager.load(manager.scene());
    EXPECT_EQ(manager.registry().get<ecs::TransformComponent>(manager.editorCamera()).position.y, 7.0f);
    EXPECT_EQ(manager.scene().objects[0].components.at("Transform").try_cast<ecs::TransformComponent>(),
        &manager.registry().get<ecs::TransformComponent>(manager.editorCamera()));
}

TEST(SceneManagerTest, SerializedSceneStoresCameraInsideAnObject)
{
    SceneManager manager;
    manager.load(makeScene());
    auto& registry = manager.registry();
    registry.get<ecs::TransformComponent>(manager.editorCamera()).position.z = 5.0f;
    registry.get<ecs::CameraComponent>(manager.editorCamera()).yaw = 30.0f;

    const auto json = rfl::json::write<rfl::SnakeCaseToPascalCase>(manager.scene());
    const auto parsed = rfl::json::read<Scene::Desc, rfl::SnakeCaseToPascalCase, rfl::NoExtraFields>(json);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->objects[0].components.at("Transform").cast<const ecs::TransformComponent&>().position.z, 5.0f);
    EXPECT_EQ(parsed->objects[0].components.at("Camera").cast<const ecs::CameraComponent&>().yaw, 30.0f);
    EXPECT_TRUE(parsed->objects[0].components.contains("FreeFlyMove"));
}

TEST(SceneManagerTest, CharacterMovementAndEnvironmentUpRoundTrip)
{
    auto scene = makeScene();
    auto& components = scene.objects[0].components;
    components.erase("FreeFlyMove");
    components.emplace("CharacterMove", ecs::CharacterMoveComponent{});
    scene.environment.up = {0.0f, 0.0f, 3.0f};

    SceneManager manager;
    manager.load(scene);
    EXPECT_TRUE(manager.registry().all_of<ecs::CharacterMoveComponent>(manager.editorCamera()));
    EXPECT_FALSE(manager.registry().all_of<ecs::FreeFlyMoveComponent>(manager.editorCamera()));

    const auto json = rfl::json::write<rfl::SnakeCaseToPascalCase>(manager.scene());
    const auto parsed = rfl::json::read<Scene::Desc, rfl::SnakeCaseToPascalCase, rfl::NoExtraFields>(json);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->environment.up, scene.environment.up);
    EXPECT_TRUE(parsed->objects[0].components.contains("CharacterMove"));
    EXPECT_FALSE(parsed->objects[0].components.contains("FreeFlyMove"));
}
