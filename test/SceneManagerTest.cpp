#include "editor/EditorCamera.h"
#include "scene/SceneManager.h"
#include "scene/actor/AFreeFlyCamera.h"

#include "asset/BuiltinAssets.h"
#include "asset/AssetManager.h"
#include "core/Serializer.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"

#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <type_traits>

namespace
{
static_assert(!std::is_abstract_v<AActor>);
static_assert(std::is_abstract_v<Component>);
static_assert(!std::derived_from<EditorCamera, AActor>);
static_assert(!std::derived_from<EditorCamera, Component>);
static_assert(std::same_as<decltype(std::declval<AActor&>().tick(0.0f)), void>);
static_assert(std::same_as<decltype(std::declval<Component&>().tick(0.0f)), void>);
static_assert(std::same_as<decltype(std::declval<SceneManager&>().tick(0.0f)), void>);

SceneAsset makeScene()
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    SceneAsset scene;
    scene.id = SceneAsset::ID{"actor-test"};
    scene.actors.push_back(std::make_unique<AFreeFlyCamera>());

    auto object                                     = std::make_unique<AStaticMesh>();
    object->name                                    = "Mesh and Light";
    object->getComponent<RenderComponent>()->meshId = MeshAsset::ID{"sphere"};
    object->addComponent<PointLightComponent>();
    scene.actors.push_back(std::move(object));
    return scene;
}

struct TestTickComponent : Component
{
    int inits = 0;
    int ticks = 0;

    void init(AActor* owner) override
    {
        Component::init(owner);
        ++inits;
    }

    void tick(float deltaTime) override
    {
        ++ticks;
        actor().transform().position.x += deltaTime;
    }
};
}  // namespace

TEST(SceneManagerTest, ActorsOwnComponentsAndRestoreTheirOwner)
{
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(makeScene());

    ASSERT_EQ(manager.scene().actors.size(), 2);
    EXPECT_EQ(&managerCamera.config(), &manager.scene().editorCamera);
    EXPECT_NE(dynamic_cast<AStaticMesh*>(manager.scene().actors[1].get()), nullptr);

    auto& actor                  = *manager.scene().actors[1];
    actor.transform().position.x = 12.0f;
    EXPECT_EQ(&actor.getComponent<TransformComponent>()->actor(), &actor);
    EXPECT_EQ(&actor.getComponent<LightComponent>()->actor(), &actor);
    EXPECT_EQ(actor.getComponent<TransformComponent>()->position.x, 12.0f);
    manager.shutdown();
}

TEST(SceneManagerTest, CsmSceneLoadsBuiltinGeometryAndOneShadowCastingSun)
{
    SceneManager manager;
    manager.init();
    const auto scene = asset_json::load<SceneAsset>(
        std::filesystem::path{RENDERLAB_SOURCE_DIR} / "assets/Scene/csm_test.json");
    manager.load(scene);
    AssetManager assets;
    size_t lightCount = 0;
    size_t objectCount = 0;
    for (const auto& actor : manager.scene().actors)
    {
        if (const auto* light = actor->getComponent<LightComponent>())
        {
            ++lightCount;
            EXPECT_NE(dynamic_cast<const DirectionalLightComponent*>(light), nullptr);
            EXPECT_TRUE(light->enabled && light->castShadow);
        }
        if (const auto* render = actor->getComponent<RenderComponent>())
        {
            ++objectCount;
            EXPECT_FALSE(assets.get<MeshAsset>(render->meshId).submeshes.empty());
        }
    }
    EXPECT_EQ(lightCount, 1u);
    EXPECT_GT(objectCount, 1u);
    EXPECT_FALSE(manager.scene().environmentMap);
    manager.shutdown();
}

TEST(SceneManagerTest, MaterialGalleryLoadsGridAndPreservesExtensionData)
{
    SceneManager manager;
    manager.init();
    const auto scene = asset_json::load<SceneAsset>(
        std::filesystem::path{RENDERLAB_SOURCE_DIR} / "assets/Scene/material_gallery.json");
    manager.load(scene);
    EXPECT_EQ(Serialize(manager.scene()), Serialize(scene));

    size_t objects = 0;
    size_t cloths = 0;
    size_t lights = 0;
    json extensions = json::object();
    for (const auto& actor : manager.scene().actors)
    {
        if (actor->getComponent<LightComponent>())
            ++lights;
        if (const auto* render = actor->getComponent<RenderComponent>())
        {
            const auto& material = render->materialOverrides.at(0);
            const auto fields = Serialize(material);
            for (const auto* field : {"clearcoat", "sheen", "anisotropy", "transmission", "volume", "ior",
                                     "specular", "iridescence", "dispersion", "emissiveStrength", "unlit",
                                     "diffuseTransmission"})
                if (!fields.at(field).is_null())
                    extensions[field] = fields.at(field);

            if (material.sheen)
            {
                EXPECT_FALSE(render->meshId.empty());
                EXPECT_NE(render->meshId, BuiltinAssets::Mesh::sphere);
                EXPECT_TRUE(material.doubleSided.value_or(false));
                ++cloths;
            }
            else
            {
                EXPECT_EQ(render->meshId, BuiltinAssets::Mesh::sphere);
                const auto& position = actor->transform().position;
                EXPECT_FLOAT_EQ(position.x, (static_cast<float>(objects % 4) - 1.5f) * 3.0f);
                EXPECT_FLOAT_EQ(position.y, (1.5f - static_cast<float>(objects / 4)) * 3.0f);
            }
            ++objects;
        }
    }
    EXPECT_EQ(objects, 16u);
    EXPECT_EQ(cloths, 1u);
    EXPECT_EQ(lights, 2u);
    EXPECT_EQ(extensions.size(), 12u);
    manager.shutdown();
}

#ifndef NDEBUG
TEST(SceneManagerTest, DestructionRequiresExplicitShutdown)
{
    EXPECT_DEATH(
        {
            SceneManager manager;
            EditorCamera managerCamera{manager.scene().editorCamera};
            manager.init();
            manager.load(makeScene());
        },
        ""
    );
}
#endif

TEST(SceneManagerTest, PickingRejectsBackgroundAndStaleActorIds)
{
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(makeScene());

    const auto id = manager.scene().actors[1]->selectionId();
    EXPECT_NE(id, 0u);
    EXPECT_EQ(manager.findActor(id), manager.scene().actors[1].get());
    EXPECT_EQ(manager.findActor(0), nullptr);

    manager.load(makeScene());
    EXPECT_EQ(manager.findActor(id), nullptr);
    EXPECT_NE(manager.scene().actors[1]->selectionId(), id);
    manager.shutdown();
}

TEST(SceneManagerTest, CreatesNamesAndDeletesRegisteredActors)
{
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(makeScene());

    const auto& type = BaseTypeInfo<AActor>::baseTypeInfoMap.at("ALight");
    EXPECT_EQ(type.parentType, std::type_index{typeid(AActor)});
    auto& first  = manager.createActor("ALight");
    auto& second = manager.createActor("ALight");
    EXPECT_EQ(first.name, "ALight1");
    EXPECT_EQ(second.name, "ALight2");
    EXPECT_EQ(&first.getComponent<LightComponent>()->actor(), &first);
    EXPECT_NE(first.selectionId(), second.selectionId());

    first.name  = "ALight3";
    auto& third = manager.createActor("ALight");
    EXPECT_EQ(third.name, "ALight1");
    const auto removedId = second.selectionId();
    manager.deleteActor(removedId);
    EXPECT_EQ(manager.findActor(removedId), nullptr);
    EXPECT_EQ(manager.findActor(first.selectionId()), &first);

    auto& mesh = manager.createActor("AStaticMesh");
    EXPECT_EQ(mesh.name, "AStaticMesh1");
    EXPECT_EQ(mesh.getComponent<RenderComponent>()->meshId, BuiltinAssets::Mesh::cube);
    const auto stored = Serialize(manager.scene());
    EXPECT_EQ(stored["actors"][2]["ALight"]["name"], "ALight3");
    manager.load(manager.scene());
    EXPECT_EQ(Serialize(manager.scene()), stored);
    manager.shutdown();
}

TEST(SceneManagerTest, EditorCameraIsIndependentOfCameraActors)
{
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(makeScene());
    auto* camera                            = &managerCamera;
    manager.scene().editorCamera.position.x = 17.0f;
    const auto firstId                      = manager.scene().actors.front()->selectionId();
    auto&      second                       = manager.createActor("AFreeFlyCamera");
    const auto secondId                     = second.selectionId();
    EXPECT_EQ(&managerCamera, camera);
    manager.deleteActor(firstId);
    manager.deleteActor(secondId);
    EXPECT_EQ(manager.scene().actors.size(), 1u);
    EXPECT_EQ(&managerCamera, camera);
    EXPECT_FLOAT_EQ(managerCamera.worldPosition().x, 17.0f);

    const auto stored = Serialize(manager.scene());
    EXPECT_EQ(stored["editorCamera"]["position"][0], 17.0f);
    EXPECT_FALSE(stored["editorCamera"].contains("components"));
    manager.load(manager.scene());
    EXPECT_EQ(Serialize(manager.scene()), stored);
    manager.shutdown();
}

TEST(SceneManagerTest, LoadingDetachesSnapshotAndCanReloadItsOwnScene)
{
    const auto   source = makeScene();
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(source);
    manager.scene().editorCamera.position.y = 7.0f;
    EXPECT_EQ(source.editorCamera.position.y, 0.0f);

    manager.load(manager.scene());
    EXPECT_EQ(manager.scene().editorCamera.position.y, 7.0f);
    EXPECT_EQ(&managerCamera.config(), &manager.scene().editorCamera);
    manager.shutdown();
}

TEST(SceneManagerTest, SwitchingScenesDiscardsUnsavedChanges)
{
    const auto first             = makeScene();
    auto       second            = makeScene();
    second.id                    = "second-scene";
    second.editorCamera.position = {1.0f, 2.0f, 3.0f};

    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(first);
    const auto oldId                                       = manager.scene().actors.front()->selectionId();
    manager.scene().actors.front()->name                   = "Unsaved rename";
    manager.scene().actors.front()->transform().position.x = 99.0f;
    manager.scene().editorCamera.speed                     = 30.0f;
    (void)manager.createActor("AStaticMesh");

    manager.load(second);
    EXPECT_EQ(Serialize(manager.scene()), Serialize(second));
    EXPECT_FALSE(manager.findActor(oldId));
    manager.load(first);
    EXPECT_EQ(Serialize(manager.scene()), Serialize(first));
    manager.shutdown();
}

TEST(SceneManagerTest, PolymorphicSceneRoundTripsInheritedFieldsAndMaterialOverrides)
{
    auto scene                                                                       = makeScene();
    scene.actors[0]->transform().position.z                                          = 5.0f;
    scene.actors[0]->getComponent<CameraComponent>()->yaw                            = 30.0f;
    scene.actors[1]->getComponent<RenderComponent>()->materialOverrides[0].roughness = 0.3f;

    const json stored = Serialize(scene);
    ASSERT_EQ(stored.at("actors")[0].size(), 1);
    const auto& cameraActor = stored.at("actors")[0].at("AFreeFlyCamera");
    EXPECT_EQ(cameraActor.at("name"), "AFreeFlyCamera");
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
    auto  scene = makeScene();
    auto& actor = *scene.actors[0];
    std::erase_if(
        actor.components,
        [](const auto& component) { return dynamic_cast<FreeFlyMoveComponent*>(component.get()) != nullptr; }
    );
    actor.addComponent<CharacterMoveComponent>();
    scene.environment.up = {0.0f, 0.0f, 3.0f};

    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(scene);
    EXPECT_NE(manager.scene().actors[0]->getComponent<CharacterMoveComponent>(), nullptr);
    EXPECT_EQ(manager.scene().actors[0]->getComponent<FreeFlyMoveComponent>(), nullptr);
    EXPECT_EQ(manager.scene().environment.up, scene.environment.up);
    manager.shutdown();
}

TEST(SceneManagerTest, ComponentTickUsesItsOwningActor)
{
    AActor actor;
    actor.init();
    auto& tick = actor.addComponent<TestTickComponent>();
    EXPECT_EQ(tick.inits, 1);
    actor.tick(0.25f);
    EXPECT_EQ(tick.ticks, 1);
    EXPECT_FLOAT_EQ(actor.transform().position.x, 0.25f);
}

TEST(SceneManagerTest, MultipleComponentsOfTheSameTypeInitializeAndTick)
{
    struct ATick : AStaticMesh
    {
        ATick()
        {
            addDefaultComponent<TestTickComponent>();
            addDefaultComponent<TestTickComponent>();
        }
    } actor;
    const auto defaults = actor.getComponents<TestTickComponent>();
    ASSERT_EQ(defaults.size(), 2);
    auto& first  = *defaults[0];
    auto& second = *defaults[1];
    EXPECT_EQ(first.inits, 0);
    EXPECT_EQ(second.inits, 0);
    actor.init();
    auto& third = actor.addComponent<TestTickComponent>();

    EXPECT_EQ(actor.getComponent<TestTickComponent>(), &first);
    EXPECT_EQ(actor.getComponents<TestTickComponent>(), (std::vector<TestTickComponent*>{&first, &second, &third}));
    for (auto* component : actor.getComponents<TestTickComponent>())
    {
        EXPECT_EQ(component->inits, 1);
        EXPECT_EQ(&component->actor(), &actor);
    }

    actor.tick(0.25f);
    for (auto* component : actor.getComponents<TestTickComponent>())
        EXPECT_EQ(component->ticks, 1);
    EXPECT_FLOAT_EQ(actor.transform().position.x, 0.75f);
}

TEST(SceneManagerTest, ComponentQueriesIncludeDerivedTypesAndPreserveConstness)
{
    struct DerivedTickComponent : TestTickComponent
    {
    };

    AStaticMesh actor;
    EXPECT_EQ(actor.getComponent<TestTickComponent>(), nullptr);
    EXPECT_TRUE(actor.getComponents<TestTickComponent>().empty());
    auto& first  = actor.addComponent<DerivedTickComponent>();
    auto& second = actor.addComponent<TestTickComponent>();
    auto& third  = actor.addComponent<DerivedTickComponent>();
    EXPECT_EQ(actor.getComponent<TestTickComponent>(), &first);
    EXPECT_EQ(actor.getComponent<DerivedTickComponent>(), &first);
    EXPECT_EQ(actor.getComponents<TestTickComponent>(), (std::vector<TestTickComponent*>{&first, &second, &third}));
    EXPECT_EQ(actor.getComponents<DerivedTickComponent>(), (std::vector<DerivedTickComponent*>{&first, &third}));

    const AActor& readOnly = actor;
    static_assert(std::same_as<decltype(readOnly.getComponent<TestTickComponent>()), const TestTickComponent*>);
    static_assert(
        std::same_as<decltype(readOnly.getComponents<TestTickComponent>()), std::vector<const TestTickComponent*>>
    );
    EXPECT_EQ(readOnly.getComponent<TestTickComponent>(), &first);
    EXPECT_EQ(
        readOnly.getComponents<TestTickComponent>(),
        (std::vector<const TestTickComponent*>{&first, &second, &third})
    );
    EXPECT_EQ(
        readOnly.getComponents<DerivedTickComponent>(),
        (std::vector<const DerivedTickComponent*>{&first, &third})
    );
    EXPECT_TRUE(readOnly.getComponents<LightComponent>().empty());
}

TEST(SceneManagerTest, RepeatedComponentTypesRoundTripAndRestoreEveryOwner)
{
    auto  scene                                          = makeScene();
    auto& object                                         = *scene.actors[1];
    object.addComponent<TransformComponent>().position.x = 8.0f;
    object.addComponent<RenderComponent>().meshId        = MeshAsset::ID{"cube"};
    object.getComponent<PointLightComponent>()->luminousIntensity     = 2.0f;
    object.addComponent<PointLightComponent>().luminousIntensity      = 7.0f;
    const auto stored                                    = Serialize(scene);

    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(scene);
    auto& restored = *manager.scene().actors[1];
    EXPECT_EQ(Serialize(manager.scene()), stored);
    const auto transforms = restored.getComponents<TransformComponent>();
    ASSERT_EQ(transforms.size(), 2);
    EXPECT_EQ(&restored.transform(), transforms[0]);
    EXPECT_FLOAT_EQ(transforms[1]->position.x, 8.0f);
    const auto renders = restored.getComponents<RenderComponent>();
    ASSERT_EQ(renders.size(), 2);
    EXPECT_EQ(renders[1]->meshId, MeshAsset::ID{"cube"});
    const auto lights = restored.getComponents<PointLightComponent>();
    ASSERT_EQ(lights.size(), 2);
    EXPECT_EQ(restored.getComponent<LightComponent>(), lights[0]);
    EXPECT_FLOAT_EQ(lights[0]->luminousIntensity, 2.0f);
    EXPECT_FLOAT_EQ(lights[1]->luminousIntensity, 7.0f);
    for (const auto& component : restored.components)
        EXPECT_EQ(&component->actor(), &restored);
    manager.shutdown();
}

TEST(SceneManagerTest, NullComponentsAreStillRejected)
{
    AStaticMesh actor;
    actor.components.push_back(nullptr);
    EXPECT_DEATH(actor.init(), "");
}

TEST(SceneManagerTest, ComponentOwnerIsBoundOnlyDuringInit)
{
    struct ADefault : AStaticMesh
    {
        ADefault() { addDefaultComponent<TestTickComponent>(); }
    } actor;
    auto& component = *actor.getComponent<TestTickComponent>();
    EXPECT_EQ(component.inits, 0);
#ifndef NDEBUG
    EXPECT_DEATH((void)component.actor(), "");
    EXPECT_DEATH((void)actor.transform().actor(), "");
    EXPECT_DEATH(component.init(nullptr), "");
    EXPECT_DEATH((void)component.actor(), "");
#endif

    actor.init();
    EXPECT_EQ(component.inits, 1);
    EXPECT_EQ(&component.actor(), &actor);
    EXPECT_EQ(&actor.transform().actor(), &actor);
}

TEST(SceneManagerTest, AddComponentInitializesImmediatelyBeforeActorInit)
{
    struct ProbeComponent : TestTickComponent
    {
        explicit ProbeComponent(int value) : value(value) {}

        void init(AActor* owner) override
        {
            TestTickComponent::init(owner);
            EXPECT_EQ(owner->getComponent<ProbeComponent>(), this);
        }

        int value;
    };

    AStaticMesh actor;
    auto&       component = actor.addComponent<ProbeComponent>(42);
    EXPECT_EQ(component.value, 42);
    EXPECT_EQ(component.inits, 1);
    EXPECT_EQ(&component.actor(), &actor);
#ifndef NDEBUG
    EXPECT_DEATH((void)actor.transform().actor(), "");
#endif
}

TEST(SceneManagerTest, InvalidAdditionFailsBeforeActorInit)
{
    AStaticMesh actor;
    actor.components.clear();
    EXPECT_DEATH(actor.addComponent<RenderComponent>(), "");
}

TEST(SceneManagerTest, ComponentDependenciesDoNotDependOnSerializedOrder)
{
    auto scene = makeScene();
    for (const auto& actor : scene.actors)
        std::reverse(actor->components.begin(), actor->components.end());

    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(scene);
    for (const auto& actor : manager.scene().actors)
        for (const auto& component : actor->components)
            EXPECT_EQ(&component->actor(), actor.get());
    EXPECT_EQ(Serialize(manager.scene()), Serialize(scene));
    manager.shutdown();
}

TEST(SceneManagerTest, ComponentsCheckTheirOwnTransformDependency)
{
    AStaticMesh actor;
    actor.components.clear();
    CameraComponent        camera;
    FreeFlyMoveComponent   freeFly;
    CharacterMoveComponent character;
    RenderComponent        render;
    PointLightComponent         light;

    EXPECT_DEATH(camera.init(&actor), "");
    EXPECT_DEATH(freeFly.init(&actor), "");
    EXPECT_DEATH(character.init(&actor), "");
    EXPECT_DEATH(render.init(&actor), "");
    EXPECT_DEATH(light.init(&actor), "");
}

TEST(SceneManagerTest, MovementInitRejectsConflictingComponentsInEitherOrder)
{
    AStaticMesh actor;
    auto&       freeFly   = actor.addDefaultComponent<FreeFlyMoveComponent>();
    auto&       character = actor.addDefaultComponent<CharacterMoveComponent>();
    EXPECT_DEATH(freeFly.init(&actor), "");
    EXPECT_DEATH(character.init(&actor), "");
    EXPECT_DEATH(actor.init(), "");
    std::reverse(actor.components.begin(), actor.components.end());
    EXPECT_DEATH(actor.init(), "");
}

TEST(SceneManagerTest, RuntimeAdditionInitializesAndRejectsInvalidDependencies)
{
    AStaticMesh actor;
    actor.init();
    auto& component = actor.addComponent<TestTickComponent>();
    EXPECT_EQ(component.inits, 1);
    EXPECT_EQ(&component.actor(), &actor);

    auto& freeFly = actor.addComponent<FreeFlyMoveComponent>();
    EXPECT_EQ(&freeFly.actor(), &actor);
    EXPECT_EQ(actor.getComponent<CameraComponent>(), nullptr);
    EXPECT_DEATH(actor.addComponent<CharacterMoveComponent>(), "");
}

TEST(SceneManagerTest, ALightRoundTripsAndTicksItsComponents)
{
    auto scene                                       = makeScene();
    auto light                                       = std::make_unique<ALight>();
    light->name                                      = "Point Light";
    light->transform().position                      = {1.0f, 2.0f, 3.0f};
    light->getComponent<PointLightComponent>()->luminousIntensity = 5.0f;
    scene.actors.push_back(std::move(light));

    const json stored = Serialize(scene);
    EXPECT_TRUE(stored.at("actors")[2].contains("ALight"));

    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    manager.load(scene);
    auto* restored = dynamic_cast<ALight*>(manager.scene().actors[2].get());
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(Serialize(manager.scene()), stored);
    EXPECT_EQ(&restored->getComponent<LightComponent>()->actor(), restored);

    auto& tick = restored->addComponent<TestTickComponent>();
    EXPECT_EQ(tick.inits, 1);
    restored->tick(0.25f);
    EXPECT_EQ(tick.ticks, 1);
    EXPECT_FLOAT_EQ(restored->transform().position.x, 1.25f);
    manager.shutdown();
}

TEST(LightPhotometry, MultipliesLinearColorWithoutNormalization)
{
    PointLightComponent light;
    light.luminousIntensity = 1000.0f;
    EXPECT_EQ(light.emissionColor(), glm::vec3{1000.0f});
    light.color = {1.0f, 0.5f, 0.25f};
    EXPECT_EQ(light.emissionColor(), (glm::vec3{1000.0f, 500.0f, 250.0f}));

    RectAreaLightComponent area;
    area.luminousIntensity = light.luminousIntensity;
    area.color = light.color;
    area.areaSize = {2, 3};
    const auto emission = area.emissionColor();
    EXPECT_NEAR(emission.x, 1000.0f / 6.0f, 0.0001f);
    EXPECT_NEAR(emission.y, 500.0f / 6.0f, 0.0001f);
    EXPECT_NEAR(emission.z, 250.0f / 6.0f, 0.0001f);
    area.areaSize.y *= 2;
    EXPECT_EQ(area.emissionColor(), emission * 0.5f);
    EXPECT_FLOAT_EQ(area.luminousIntensity, 1000.0f);

    DirectionalLightComponent directional;
    directional.illuminance = light.luminousIntensity;
    directional.color = light.color;
    EXPECT_EQ(directional.emissionColor(), light.emissionColor());

    SpotLightComponent spot;
    spot.luminousIntensity = light.luminousIntensity;
    spot.color = light.color;
    EXPECT_EQ(spot.emissionColor(), light.emissionColor());
    spot.color = glm::vec3{0.0f};
    EXPECT_EQ(spot.emissionColor(), glm::vec3{0.0f});
}

TEST(SceneManagerTest, ALightRequiresLightComponent)
{
    ALight actor;
    std::erase_if(
        actor.components,
        [](const auto& component) { return dynamic_cast<LightComponent*>(component.get()) != nullptr; }
    );
    EXPECT_DEATH(actor.init(), "");
}

TEST(SceneManagerTest, BaseTypesAreNotRegistered)
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    EXPECT_FALSE(BaseTypeInfo<AActor>::baseTypeInfoMap.contains("AActor"));
    EXPECT_FALSE(BaseTypeInfo<Component>::baseTypeInfoMap.contains("Component"));
}

TEST(SceneManagerTest, RejectsUnknownTypesMissingFieldsAndMissingRequiredComponents)
{
    auto stored         = Serialize(makeScene());
    stored["actors"][0] = json{{"MissingActor", stored["actors"][0].at("AFreeFlyCamera")}};
    SceneAsset parsed;
    EXPECT_DEATH(Deserialize(stored, parsed), "");

    stored           = Serialize(makeScene());
    auto& components = stored["actors"][0]["AFreeFlyCamera"]["components"];
    components[1]    = json{{"MissingComponent", components[1].at("CameraComponent")}};
    EXPECT_DEATH(Deserialize(stored, parsed), "");

    stored = Serialize(makeScene());
    stored["actors"][0]["AFreeFlyCamera"].erase("name");
    EXPECT_DEATH(Deserialize(stored, parsed), "");

    stored = Serialize(makeScene());
    stored["actors"][0]["AFreeFlyCamera"]["components"].erase(0);
    Deserialize(stored, parsed);
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    EXPECT_DEATH(manager.load(parsed), "");
    manager.shutdown();
}

TEST(SceneManagerTest, PolymorphicPointersRequireExactlyOneTypeKey)
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    std::unique_ptr<AActor>    actor             = std::make_unique<AStaticMesh>();
    std::unique_ptr<Component> component         = std::make_unique<TransformComponent>();
    const auto*                originalActor     = actor.get();
    const auto*                originalComponent = component.get();

    const std::vector<json> invalid = {
        json::object(),
        json::array(),
        json::array({json::object()}),
        "AStaticMesh",
        42,
        true,
        json{{"AStaticMesh", json::object()}, {"TransformComponent", json::object()}},
    };
    for (const auto& stored : invalid)
    {
        SCOPED_TRACE(stored.dump());
        EXPECT_DEATH(Deserialize(stored, actor), "");
        EXPECT_DEATH(Deserialize(stored, component), "");
        EXPECT_EQ(actor.get(), originalActor);
        EXPECT_EQ(component.get(), originalComponent);
    }
}

TEST(SceneManagerTest, PolymorphicPointersRequireObjectData)
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    std::unique_ptr<AActor>    actor;
    std::unique_ptr<Component> component;

    const std::vector<json> invalid = {nullptr, false, 42, "not an object", json::array()};
    for (const auto& data : invalid)
    {
        SCOPED_TRACE(data.dump());
        EXPECT_DEATH(Deserialize(json{{"AStaticMesh", data}}, actor), "");
        EXPECT_DEATH(Deserialize(json{{"TransformComponent", data}}, component), "");
        EXPECT_EQ(actor, nullptr);
        EXPECT_EQ(component, nullptr);
    }
}

TEST(SceneManagerTest, LightTypeUsesConcreteComponentTag)
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    forEachLightComponentType([]<class T>(const char* name)
    {
        std::unique_ptr<Component> component = std::make_unique<T>();
        const json stored = Serialize(component);
        ASSERT_EQ(stored.size(), 1);
        EXPECT_TRUE(stored.contains(name));
        const auto& fields = stored.at(name);
        EXPECT_FALSE(fields.contains("type"));
        EXPECT_EQ(fields.contains("range"), (std::derived_from<T, LocalLightComponent>));
        EXPECT_EQ(fields.contains("areaSize"), (std::same_as<T, RectAreaLightComponent>));
        EXPECT_EQ(fields.contains("cosInner"), (std::same_as<T, SpotLightComponent>));
        EXPECT_EQ(fields.contains("cosOuter"), (std::same_as<T, SpotLightComponent>));

        std::unique_ptr<Component> restored;
        Deserialize(stored, restored);
        ASSERT_NE(dynamic_cast<T*>(restored.get()), nullptr);
        EXPECT_EQ(Serialize(restored), stored);
    });
}

TEST(SceneManagerTest, RegistryIteratesInheritedMembersAndRejectsDuplicateRegistration)
{
    SceneManager registration;
    registration.init();
    registration.shutdown();
    AFreeFlyCamera                actor;
    std::vector<std::string_view> members;
    const auto&                   type = BaseTypeInfo<AActor>::baseTypeInfoMap.at("AFreeFlyCamera");
    type.IterateMembers(actor, [&](MemberRef member) { members.push_back(member.name); });
    EXPECT_EQ(members, (std::vector<std::string_view>{"name", "components"}));
#ifndef NDEBUG
    EXPECT_DEATH((RegisterBase<TransformComponent, Component>("TransformComponent")), "");
#endif
}

TEST(SceneManagerTest, NullPolymorphicPointersRoundTripButNullActorsAreRejected)
{
    std::unique_ptr<Component> component;
    EXPECT_TRUE(Serialize(component).is_null());
    component = std::make_unique<PointLightComponent>();
    Deserialize(json(nullptr), component);
    EXPECT_EQ(component, nullptr);

    auto scene = makeScene();
    scene.actors.push_back(nullptr);
    SceneManager manager;
    EditorCamera managerCamera{manager.scene().editorCamera};
    manager.init();
    EXPECT_DEATH(manager.load(scene), "");
    manager.shutdown();
}
