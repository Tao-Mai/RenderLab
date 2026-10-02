#include "ecs/SceneManager.h"

#include "asset/AssetDescManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "ecs/component/CameraComponent.h"
#include "ecs/component/LightComponent.h"
#include "ecs/component/FreeFlyMoveComponent.h"
#include "ecs/component/CharacterMoveComponent.h"
#include "ecs/component/RenderComponent.h"
#include "ecs/component/TransformComponent.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
template <class F>
void forEachComponent(F&& function)
{
    function.template operator()<ecs::TransformComponent>();
    function.template operator()<ecs::CameraComponent>();
    function.template operator()<ecs::FreeFlyMoveComponent>();
    function.template operator()<ecs::CharacterMoveComponent>();
    function.template operator()<ecs::RenderComponent>();
    function.template operator()<ecs::LightComponent>();
}
}

void SceneManager::load(const Scene::ID& id)
{
    CHECK(context().assetDescManager != nullptr, "SceneManager requires AssetDescManager");
    load(context().assetDescManager->desc<Scene>(id));
}

void SceneManager::load(const Scene::Desc& scene)
{
    // Detach references before clearing the old registry, including when reloading this scene.
    auto loaded = copyScene(scene);
    reset();
    data = std::move(loaded);
    objectEntities.reserve(data.objects.size());

    for (auto& object : data.objects)
    {
        const entt::entity entity = entityRegistry.create();
        objectEntities.push_back(entity);

        for (auto& [name, component] : object.components)
        {
            bool registered = false;
            forEachComponent([&]<class T>
            {
                if (component.type() != entt::resolve<T>()) return;
                CHECK(entt::hashed_string{name.c_str()} == entt::resolve<T>().id(),
                    "component name '{}' does not match its type", name);

                auto& value = entityRegistry.emplace<T>(entity, component.cast<const T&>());
                component = entt::forward_as_meta(value);
                registered = true;
            });
            CHECK(registered, "unsupported component '{}' on object '{}'", name, object.name);
        }

        CHECK((!entityRegistry.any_of<ecs::RenderComponent, ecs::LightComponent,
            ecs::CameraComponent, ecs::FreeFlyMoveComponent, ecs::CharacterMoveComponent>(entity) ||
            entityRegistry.all_of<ecs::TransformComponent>(entity)),
            "object '{}' requires Transform", object.name);

        CHECK((!entityRegistry.all_of<ecs::FreeFlyMoveComponent, ecs::CharacterMoveComponent>(entity)),
            "object '{}' cannot have both FreeFlyMove and CharacterMove", object.name);

        if (object.name == editorCameraName)
        {
            CHECK(cameraEntity == entt::null, "duplicate editor camera object");
            CHECK((entityRegistry.all_of<ecs::TransformComponent, ecs::CameraComponent>(entity) &&
                entityRegistry.any_of<ecs::FreeFlyMoveComponent, ecs::CharacterMoveComponent>(entity) &&
                !entityRegistry.any_of<ecs::RenderComponent>(entity)),
                "editor camera requires Transform, Camera and one movement component, without Render");
            cameraEntity = entity;
        }
    }

    CHECK(cameraEntity != entt::null, "scene requires an '{}' object", editorCameraName);
}

Scene::Desc SceneManager::copyScene(const Scene::Desc& scene)
{
    Scene::Desc copy;
    copy.id = scene.id;
    copy.environment = scene.environment;
    copy.objects.reserve(scene.objects.size());

    for (const auto& source : scene.objects)
    {
        auto& object = copy.objects.emplace_back();
        object.name = source.name;
        for (const auto& [name, component] : source.components)
        {
            bool copied = false;
            forEachComponent([&]<class T>
            {
                if (component.type() != entt::resolve<T>()) return;
                object.components.emplace(name, entt::meta_any{component.cast<const T&>()});
                copied = true;
            });
            CHECK(copied, "unsupported component '{}' on object '{}'", name, object.name);
        }
    }
    return copy;
}

void SceneManager::save()
{
    CHECK(context().assetDescManager != nullptr, "SceneManager requires AssetDescManager");
    // The asset cache owns snapshots, never references into this registry.
    context().assetDescManager->save<Scene>(copyScene(data));
}

void SceneManager::reset() noexcept
{
    data = {};
    objectEntities.clear();
    cameraEntity = entt::null;
    entityRegistry.clear();
}

Scene::Desc& SceneManager::scene() noexcept { return data; }
const Scene::Desc& SceneManager::scene() const noexcept { return data; }
entt::registry& SceneManager::registry() noexcept { return entityRegistry; }
const entt::registry& SceneManager::registry() const noexcept { return entityRegistry; }
std::span<const entt::entity> SceneManager::entities() const noexcept { return objectEntities; }
entt::entity SceneManager::editorCamera() const noexcept { return cameraEntity; }

uint32_t SceneManager::selectionId(entt::entity entity)
{
    const auto id = entt::to_integral(entity);
    CHECK(id < std::numeric_limits<uint32_t>::max(), "invalid picking entity");
    return id + 1;
}

Scene::Desc::Object* SceneManager::findObject(uint32_t selectionId)
{
    if (selectionId == 0) return nullptr;

    const auto entity = static_cast<entt::entity>(selectionId - 1);
    const auto found = std::ranges::find(objectEntities, entity);
    if (found == objectEntities.end() || !entityRegistry.valid(entity)) return nullptr;

    return &data.objects[static_cast<size_t>(found - objectEntities.begin())];
}
