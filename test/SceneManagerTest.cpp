#include "scene/SceneManager.h"

#include "asset/JsonIo.h"
#include "scene/actor/ALight.h"
#include "scene/actor/StaticMeshActor.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"

#include <algorithm>
#include <type_traits>
#include <gtest/gtest.h>

namespace
{
static_assert(std::is_abstract_v<Actor>);
static_assert(std::is_abstract_v<Component>);
static_assert(std::same_as<decltype(std::declval<Actor&>().tick(0.0f)), void>);
static_assert(std::same_as<decltype(std::declval<Component&>().tick(0.0f)), void>);
static_assert(std::same_as<decltype(std::declval<SceneManager&>().tick(0.0f)), void>);

SceneAsset makeScene()
{
    RegisterSceneTypes();
    SceneAsset scene;
    scene.id = SceneAsset::ID{"actor-test"};
    scene.actors.push_back(std::make_unique<FreeFlyCameraActor>());

    auto object = std::make_unique<StaticMeshActor>();
    object->name = "Mesh and Light";
    object->getComponent<RenderComponent>()->meshId = MeshAsset::ID{"sphere"};
    object->addComponent<LightComponent>();
    scene.actors.push_back(std::move(object));
    return scene;
}

struct TestTickComponent : Component
{
    int ticks = 0;

    void tick(float deltaTime) override
    {
        ++ticks;
        actor().transform().position.x += deltaTime;
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

    SceneAsset restored;
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
    StaticMeshActor actor;
    auto& tick = actor.addComponent<TestTickComponent>();
    actor.tick(0.25f);
    EXPECT_EQ(tick.ticks, 1);
    EXPECT_FLOAT_EQ(actor.transform().position.x, 0.25f);
    EXPECT_THROW(actor.addComponent<TestTickComponent>(), std::logic_error);
}

TEST(SceneManagerTest, ALightRoundTripsAndTicksItsComponents)
{
    auto scene = makeScene();
    auto light = std::make_unique<ALight>();
    light->name = "Point Light";
    light->transform().position = {1.0f, 2.0f, 3.0f};
    light->getComponent<LightComponent>()->intensity = 5.0f;
    scene.actors.push_back(std::move(light));

    const json stored = Serialize(scene);
    EXPECT_TRUE(stored.at("actors")[2].contains("ALight"));

    SceneManager manager;
    manager.load(scene);
    auto* restored = dynamic_cast<ALight*>(manager.scene().actors[2].get());
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(Serialize(manager.scene()), stored);
    EXPECT_EQ(&restored->getComponent<LightComponent>()->actor(), restored);

    auto& tick = restored->addComponent<TestTickComponent>();
    restored->tick(0.25f);
    EXPECT_EQ(tick.ticks, 1);
    EXPECT_FLOAT_EQ(restored->transform().position.x, 1.25f);
}

TEST(SceneManagerTest, ALightRequiresLightComponent)
{
    ALight actor;
    std::erase_if(actor.components, [](const auto& component)
    {
        return dynamic_cast<LightComponent*>(component.get()) != nullptr;
    });
    EXPECT_THROW(actor.initialize(), std::logic_error);
}

TEST(SceneManagerTest, AbstractBaseTypesAreNotRegistered)
{
    RegisterSceneTypes();
    EXPECT_FALSE(BaseTypeInfo<Actor>::baseTypeInfoMap.contains("Actor"));
    EXPECT_FALSE(BaseTypeInfo<Component>::baseTypeInfoMap.contains("Component"));
}

TEST(SceneManagerTest, RejectsUnknownTypesMissingFieldsAndMissingRequiredComponents)
{
    auto stored = Serialize(makeScene());
    stored["actors"][0] = json{{"MissingActor", stored["actors"][0].at("FreeFlyCameraActor")}};
    SceneAsset parsed;
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
    std::unique_ptr<Actor> actor = std::make_unique<StaticMeshActor>();
    std::unique_ptr<Component> component = std::make_unique<TransformComponent>();
    const auto* originalActor = actor.get();
    const auto* originalComponent = component.get();

    const std::vector<json> invalid = {
        json::object(), json::array(), json::array({json::object()}),
        "StaticMeshActor", 42, true,
        json{{"StaticMeshActor", json::object()}, {"TransformComponent", json::object()}},
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
        EXPECT_THROW(Deserialize(json{{"StaticMeshActor", data}}, actor), json::other_error);
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
