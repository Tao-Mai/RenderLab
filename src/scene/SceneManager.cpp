#include "scene/SceneManager.h"

#include "asset/AssetManager.h"
#include "core/Serializer.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "scene/SceneTypes.h"

#include <algorithm>
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
            if (!camera) camera = candidate;
            else candidate->camera().setInputEnabled(false);
        }

        CHECK(nextSelectionId != std::numeric_limits<uint32_t>::max(), "scene selection IDs exhausted");
        actor->id = nextSelectionId++;
    }

    if (cameraActor) cameraActor->camera().resetNavigation();
    navigationCamera.reset();
    if (!camera)
    {
        navigationCamera = std::make_unique<AFreeFlyCamera>();
        navigationCamera->init();
        camera = navigationCamera.get();
    }

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

AActor& SceneManager::createActor(std::string_view typeName)
{
    DCHECK(inited);
    const auto& types = BaseTypeInfo<AActor>::baseTypeInfoMap;
    const auto type = types.find(std::string{typeName});
    CHECK(type != types.end(), "unknown actor type: {}", typeName);

    auto actor = type->second.Create();
    uint64_t index = 1;
    do
    {
        actor->name = std::string{typeName} + std::to_string(index++);
    } while (std::ranges::any_of(data.actors, [&](const auto& existing) {
        return existing->name == actor->name;
    }));

    actor->init();
    CHECK(nextSelectionId != std::numeric_limits<uint32_t>::max(), "scene selection IDs exhausted");
    actor->id = nextSelectionId++;
    auto& result = *actor;
    data.actors.push_back(std::move(actor));

    if (auto* camera = dynamic_cast<AFreeFlyCamera*>(&result))
    {
        if (navigationCamera)
        {
            navigationCamera->camera().resetNavigation();
            cameraActor = camera;
            navigationCamera.reset();
        }
        else camera->camera().setInputEnabled(false);
    }
    return result;
}

void SceneManager::deleteActor(uint32_t selectionId)
{
    DCHECK(inited);
    const auto found = std::ranges::find_if(data.actors, [=](const auto& actor) {
        return actor->selectionId() == selectionId;
    });
    if (found == data.actors.end()) return;

    if (found->get() == cameraActor)
    {
        cameraActor->camera().resetNavigation();
        AFreeFlyCamera* nextCamera = nullptr;
        for (const auto& actor : data.actors)
        {
            if (actor.get() == cameraActor) continue;
            if (auto* camera = dynamic_cast<AFreeFlyCamera*>(actor.get()))
            {
                nextCamera = camera;
                break;
            }
        }

        if (nextCamera)
        {
            cameraActor = nextCamera;
            cameraActor->camera().setInputEnabled(true);
        }
        else
        {
            // Navigation survives deletion, but the removed actor and its components do not.
            navigationCamera = std::make_unique<AFreeFlyCamera>();
            Deserialize(Serialize(cameraActor->transform()), navigationCamera->transform());
            Deserialize(Serialize(cameraActor->camera()), navigationCamera->camera());
            if (const auto* movement = cameraActor->getComponent<FreeFlyMoveComponent>())
                Deserialize(Serialize(*movement), *navigationCamera->getComponent<FreeFlyMoveComponent>());
            navigationCamera->init();
            cameraActor = navigationCamera.get();
        }
    }
    data.actors.erase(found);
}

void SceneManager::shutdown() noexcept
{
    if (cameraActor)
        cameraActor->camera().resetNavigation();
    cameraActor = nullptr;
    navigationCamera.reset();
    data        = {};
    DEBUG_EXEC(inited = false);
}

void SceneManager::tick(float deltaTime)
{
    auto& camera = editorCamera();
    camera.tick(deltaTime);
    for (const auto& actor : data.actors)
        if (actor.get() != &camera)
        {
            if (auto* otherCamera = dynamic_cast<AFreeFlyCamera*>(actor.get()))
                otherCamera->camera().setInputEnabled(false);
            actor->tick(deltaTime);
        }
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
