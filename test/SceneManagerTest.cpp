#include "scene/SceneManager.h"

#include "asset/JsonIo.h"
#include "scene/actor/StaticMeshActor.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace
{
Scene::Desc makeScene()
{
    RegisterSceneTypes();
    Scene::Desc scene;
    scene.id = Scene::ID{"actor-test"};
    scene.actors.push_back(std::make_unique<FreeFlyCameraActor>());

    auto object = std::make_unique<StaticMeshActor>();
    object->name = "Mesh and Light";
    object->getComponent<RenderComponent>()->meshId = Mesh::ID{"sphere"};
    object->addComponent<LightComponent>();
    scene.actors.push_back(std::move(object));
    return scene;
}

struct TestTickComponent : Component
{
    int ticks = 0;

    bool tick(float deltaTime) override
    {
        ++ticks;
        actor().transform().position.x += deltaTime;
        return true;
    }
};
}

TEST(SceneManagerTest, ActorsOwnComponentsAndRestoreTheirOwner)
{
    SceneManager manager;
    manager.load(makeScene());

    ASSERT_EQ(manager.scene().actors.size(), 2);
    EXPECT_NE(manager.editorCamera().getComponent<CameraComponent>(), nullptr);
    EXPECT_EQ(manager.editorCamera().getComponent<RenderComponent>(), nullptr);
    EXPECT_NE(dynamic_cast<StaticMeshActor*>(manager.scene().actors[1].get()), nullptr);

    auto& actor = *manager.scene().actors[1];
    actor.transform().position.x = 12.0f;
    EXPECT_EQ(&actor.getComponent<TransformComponent>()->actor(), &actor);
    EXPECT_EQ(&actor.getComponent<LightComponent>()->actor(), &actor);
    EXPECT_EQ(actor.getComponent<TransformComponent>()->position.x, 12.0f);
}

TEST(SceneManagerTest, PickingRejectsBackgroundAndStaleActorIds)
{
    SceneManager manager;
    manager.load(makeScene());

    const auto id = manager.scene().actors[1]->selectionId();
    EXPECT_NE(id, 0u);
    EXPECT_EQ(manager.findActor(id), manager.scene().actors[1].get());
    EXPECT_EQ(manager.findActor(0), nullptr);

    manager.load(makeScene());
    EXPECT_EQ(manager.findActor(id), nullptr);
    EXPECT_NE(manager.scene().actors[1]->selectionId(), id);
}

TEST(SceneManagerTest, LoadingDetachesSnapshotAndCanReloadItsOwnScene)
{
    const auto source = makeScene();
    SceneManager manager;
    manager.load(source);
    manager.editorCamera().transform().position.y = 7.0f;
    EXPECT_EQ(source.actors[0]->transform().position.y, 0.0f);

    manager.load(manager.scene());
    EXPECT_EQ(manager.editorCamera().transform().position.y, 7.0f);
    EXPECT_EQ(&manager.editorCamera().camera().actor(), &manager.editorCamera());
}

TEST(SceneManagerTest, PolymorphicSceneRoundTripsInheritedFieldsAndMaterialOverrides)
{
    auto scene = makeScene();
    scene.actors[0]->transform().position.z = 5.0f;
    scene.actors[0]->getComponent<CameraComponent>()->yaw = 30.0f;
    scene.actors[1]->getComponent<RenderComponent>()->materialOverrides[0].roughness = 0.3f;

    const json stored = Serialize(scene);
    ASSERT_EQ(stored.at("actors")[0].size(), 1);
    const auto& cameraActor = stored.at("actors")[0].at("FreeFlyCameraActor");
    EXPECT_EQ(cameraActor.at("name"), "Editor Camera");
    EXPECT_EQ(cameraActor.size(), 2);
    ASSERT_EQ(cameraActor.at("components")[1].size(), 1);
    EXPECT_FALSE(cameraActor.at("components")[1].at("CameraComponent").contains("looking"));

    Scene::Desc restored;
    Deserialize(stored, restored);
    EXPECT_EQ(Serialize(restored), stored);
    EXPECT_EQ(restored.actors[0]->transform().position.z, 5.0f);
    EXPECT_EQ(restored.actors[0]->getComponent<CameraComponent>()->yaw, 30.0f);
    EXPECT_EQ(restored.actors[1]->getComponent<RenderComponent>()->materialOverrides[0].roughness, 0.3f);
}

TEST(SceneManagerTest, CharacterMovementAndEnvironmentUpRoundTrip)
{
    auto scene = makeScene();
    auto& actor = *scene.actors[0];
    std::erase_if(actor.components, [](const auto& component)
    {
        return dynamic_cast<FreeFlyMoveComponent*>(component.get()) != nullptr;
    });
    actor.addComponent<CharacterMoveComponent>();
    scene.environment.up = {0.0f, 0.0f, 3.0f};

    SceneManager manager;
    manager.load(scene);
    EXPECT_NE(manager.editorCamera().getComponent<CharacterMoveComponent>(), nullptr);
    EXPECT_EQ(manager.editorCamera().getComponent<FreeFlyMoveComponent>(), nullptr);
    EXPECT_EQ(manager.scene().environment.up, scene.environment.up);
}

TEST(SceneManagerTest, ComponentTickUsesItsOwningActor)
{
    Actor actor;
    auto& tick = actor.addComponent<TestTickComponent>();
    EXPECT_TRUE(actor.tick(0.25f));
    EXPECT_EQ(tick.ticks, 1);
    EXPECT_FLOAT_EQ(actor.transform().position.x, 0.25f);
    EXPECT_THROW(actor.addComponent<TestTickComponent>(), std::logic_error);
}

TEST(SceneManagerTest, RejectsUnknownTypesMissingFieldsAndMissingRequiredComponents)
{
    auto stored = Serialize(makeScene());
    stored["actors"][0] = json{{"MissingActor", stored["actors"][0].at("FreeFlyCameraActor")}};
    Scene::Desc parsed;
    EXPECT_THROW(Deserialize(stored, parsed), json::other_error);

    stored = Serialize(makeScene());
    auto& components = stored["actors"][0]["FreeFlyCameraActor"]["components"];
    components[1] = json{{"MissingComponent", components[1].at("CameraComponent")}};
    EXPECT_THROW(Deserialize(stored, parsed), json::other_error);

    stored = Serialize(makeScene());
    stored["actors"][0]["FreeFlyCameraActor"].erase("name");
    EXPECT_THROW(Deserialize(stored, parsed), json::out_of_range);

    stored = Serialize(makeScene());
    stored["actors"][0]["FreeFlyCameraActor"]["components"].erase(0);
    Deserialize(stored, parsed);
    SceneManager manager;
    EXPECT_THROW(manager.load(parsed), std::logic_error);
}

TEST(SceneManagerTest, PolymorphicPointersRequireExactlyOneTypeKey)
{
    RegisterSceneTypes();
    std::unique_ptr<Actor> actor = std::make_unique<Actor>();
    std::unique_ptr<Component> component = std::make_unique<TransformComponent>();
    const auto* originalActor = actor.get();
    const auto* originalComponent = component.get();

    const std::vector<json> invalid = {
        json::object(), json::array(), json::array({json::object()}),
        "Actor", 42, true,
        json{{"Actor", json::object()}, {"TransformComponent", json::object()}},
    };
    for (const auto& stored : invalid)
    {
        SCOPED_TRACE(stored.dump());
        EXPECT_THROW(Deserialize(stored, actor), json::other_error);
        EXPECT_THROW(Deserialize(stored, component), json::other_error);
        EXPECT_EQ(actor.get(), originalActor);
        EXPECT_EQ(component.get(), originalComponent);
    }
}

TEST(SceneManagerTest, PolymorphicPointersRequireObjectData)
{
    RegisterSceneTypes();
    std::unique_ptr<Actor> actor;
    std::unique_ptr<Component> component;

    const std::vector<json> invalid = {nullptr, false, 42, "not an object", json::array()};
    for (const auto& data : invalid)
    {
        SCOPED_TRACE(data.dump());
        EXPECT_THROW(Deserialize(json{{"Actor", data}}, actor), json::other_error);
        EXPECT_THROW(Deserialize(json{{"TransformComponent", data}}, component), json::other_error);
        EXPECT_EQ(actor, nullptr);
        EXPECT_EQ(component, nullptr);
    }
}

TEST(SceneManagerTest, LightTypeRemainsInsideComponentData)
{
    RegisterSceneTypes();
    std::unique_ptr<Component> component = std::make_unique<LightComponent>();
    const json stored = Serialize(component);
    ASSERT_EQ(stored.size(), 1);
    EXPECT_EQ(stored.at("LightComponent").at("type"), "Point");

    std::unique_ptr<Component> restored;
    Deserialize(stored, restored);
    ASSERT_NE(dynamic_cast<LightComponent*>(restored.get()), nullptr);
    EXPECT_EQ(Serialize(restored), stored);
}

TEST(SceneManagerTest, RegistryIteratesInheritedMembersAndRejectsDuplicateRegistration)
{
    RegisterSceneTypes();
    FreeFlyCameraActor actor;
    std::vector<std::string_view> members;
    const auto& type = BaseTypeInfo<Actor>::baseTypeInfoMap.at("FreeFlyCameraActor");
    type.IterateMembers(actor, [&](MemberRef member) { members.push_back(member.name); });
    EXPECT_EQ(members, (std::vector<std::string_view>{"name", "components"}));
    EXPECT_THROW((RegisterBase<TransformComponent, Component>("TransformComponent")), std::logic_error);
}

TEST(SceneManagerTest, NullPolymorphicPointersRoundTripButNullActorsAreRejected)
{
    std::unique_ptr<Component> component;
    EXPECT_TRUE(Serialize(component).is_null());
    component = std::make_unique<LightComponent>();
    Deserialize(json(nullptr), component);
    EXPECT_EQ(component, nullptr);

    auto scene = makeScene();
    scene.actors.push_back(nullptr);
    SceneManager manager;
    EXPECT_THROW(manager.load(scene), std::logic_error);
}
