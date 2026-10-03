#include "scene/SceneManager.h"

#include "asset/AssetManager.h"
#include "core/Serializer.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "scene/SceneTypes.h"

#include <limits>

SceneManager::~SceneManager()
{
    DCHECK(!inited);
}

void SceneManager::init()
{
    DCHECK(!inited);

    static const bool registered = []
    {
        forEachComponentType([]<class T>(std::string_view name) { RegisterBase<T, Component>(name); });
        forEachActorType([]<class T>(std::string_view name) { RegisterBase<T, AActor>(name); });
        return true;
    }();

    DEBUG_EXEC(inited = true);
}

void SceneManager::loadDefaultScene()
{
    DCHECK(inited);
    DCHECK(context().config);

    load(context().config->initialScene());
}

void SceneManager::load(const SceneAsset::ID& id)
{
    DCHECK(inited);
    DCHECK(context().assetManager);
    load(context().assetManager->get<SceneAsset>(id));
}

void SceneManager::load(const SceneAsset& scene)
{
    DCHECK(inited);

    // The cache owns a snapshot; the live scene owns its actors and components.
    SceneAsset loaded;
    Deserialize(Serialize(scene), loaded);
    AFreeFlyCamera* camera = nullptr;
    for (const auto& actor : loaded.actors)
    {
        CHECK(actor);
        actor->init();
        if (auto* candidate = dynamic_cast<AFreeFlyCamera*>(actor.get()))
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
    DCHECK(inited);
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
    DEBUG_EXEC(inited = false);
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

AFreeFlyCamera& SceneManager::editorCamera() const
{
    DCHECK(cameraActor);
    return *cameraActor;
}

AActor* SceneManager::findActor(uint32_t selectionId) const noexcept
{
    if (selectionId == 0)
        return nullptr;
    for (const auto& actor : data.actors)
        if (actor->selectionId() == selectionId)
            return actor.get();

    return nullptr;
}
