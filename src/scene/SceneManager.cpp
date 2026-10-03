#include "scene/SceneManager.h"

#include "asset/AssetManager.h"
#include "asset/Serializer.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <limits>

SceneManager::~SceneManager()
{
    DCHECK(!cameraActor && data.actors.empty());
}

void SceneManager::load(const SceneAsset::ID& id)
{
    DCHECK(context().assetManager);
    load(context().assetManager->get<SceneAsset>(id));
}

void SceneManager::load(const SceneAsset& scene)
{
    RegisterSceneTypes();

    // The cache owns a snapshot; the live scene owns its actors and components.
    SceneAsset loaded;
    Deserialize(Serialize(scene), loaded);
    FreeFlyCameraActor* camera = nullptr;
    for (const auto& actor : loaded.actors)
    {
        CHECK(actor);
        actor->init();
        if (auto* candidate = dynamic_cast<FreeFlyCameraActor*>(actor.get()))
        {
            CHECK(!camera, "scene contains multiple camera actors");
            camera = candidate;
        }

        CHECK(nextSelectionId != std::numeric_limits<uint32_t>::max(), "scene selection IDs exhausted");
        actor->id = nextSelectionId++;
    }

    CHECK(camera);

    data        = std::move(loaded);
    cameraActor = camera;
}

void SceneManager::save()
{
    DCHECK(context().assetManager);
    SceneAsset snapshot;
    Deserialize(Serialize(data), snapshot);
    context().assetManager->save<SceneAsset>(std::move(snapshot));
}

void SceneManager::shutdown() noexcept
{
    if (cameraActor)
        cameraActor->camera().resetNavigation();
    cameraActor = nullptr;
    data        = {};
}

void SceneManager::tick(float deltaTime)
{
    auto& camera = editorCamera();
    camera.tick(deltaTime);
    for (const auto& actor : data.actors)
        if (actor.get() != &camera)
            actor->tick(deltaTime);
}

SceneAsset&       SceneManager::scene() noexcept { return data; }
const SceneAsset& SceneManager::scene() const noexcept { return data; }

FreeFlyCameraActor& SceneManager::editorCamera() const
{
    DCHECK(cameraActor);
    return *cameraActor;
}

Actor* SceneManager::findActor(uint32_t selectionId) const noexcept
{
    if (selectionId == 0)
        return nullptr;
    for (const auto& actor : data.actors)
        if (actor->selectionId() == selectionId)
            return actor.get();

    return nullptr;
}
