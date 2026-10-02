#include "scene/SceneManager.h"

#include "asset/AssetDescManager.h"
#include "asset/Serializer.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <limits>
#include <stdexcept>

void SceneManager::load(const Scene::ID& id)
{
    CHECK(context().assetDescManager != nullptr, "SceneManager requires AssetDescManager");
    load(context().assetDescManager->desc<Scene>(id));
}

void SceneManager::load(const Scene::Desc& scene)
{
    RegisterSceneTypes();

    // The cache owns a snapshot; the live scene owns its actors and components.
    Scene::Desc loaded;
    Deserialize(Serialize(scene), loaded);
    FreeFlyCameraActor* camera = nullptr;
    for (const auto& actor : loaded.actors)
    {
        if (!actor) throw std::logic_error("scene contains a null actor");
        actor->initialize();
        if (auto* candidate = dynamic_cast<FreeFlyCameraActor*>(actor.get()))
        {
            if (camera) throw std::logic_error("scene contains multiple camera actors");
            camera = candidate;
        }

        if (nextSelectionId == std::numeric_limits<uint32_t>::max())
            throw std::overflow_error("scene selection IDs exhausted");
        actor->id = nextSelectionId++;
    }

    if (!camera) throw std::logic_error("scene requires a FreeFlyCameraActor");

    data = std::move(loaded);
    cameraActor = camera;
}

void SceneManager::save()
{
    CHECK(context().assetDescManager != nullptr, "SceneManager requires AssetDescManager");
    Scene::Desc snapshot;
    Deserialize(Serialize(data), snapshot);
    context().assetDescManager->save<Scene>(std::move(snapshot));
}

void SceneManager::reset() noexcept
{
    if (cameraActor) cameraActor->camera().resetNavigation();
    cameraActor = nullptr;
    data = {};
}

bool SceneManager::tick(float deltaTime)
{
    auto& camera = editorCamera();
    bool changed = camera.tick(deltaTime);
    for (const auto& actor : data.actors)
        if (actor.get() != &camera)
            changed = actor->tick(deltaTime) || changed;

    return changed;
}

Scene::Desc& SceneManager::scene() noexcept { return data; }
const Scene::Desc& SceneManager::scene() const noexcept { return data; }

FreeFlyCameraActor& SceneManager::editorCamera() const
{
    if (!cameraActor) throw std::logic_error("scene has no camera actor");
    return *cameraActor;
}

Actor* SceneManager::findActor(uint32_t selectionId) const noexcept
{
    if (selectionId == 0) return nullptr;
    for (const auto& actor : data.actors)
        if (actor->selectionId() == selectionId) return actor.get();

    return nullptr;
}
