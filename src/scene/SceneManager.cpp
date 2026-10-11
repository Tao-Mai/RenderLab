#include "scene/SceneManager.h"

#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Serializer.h"
#include "scene/SceneTypes.h"

#include <algorithm>
#include <limits>

SceneManager::~SceneManager() { DCHECK(!inited); }

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
    for (const auto& actor : loaded.actors)
    {
        CHECK(actor);
        actor->init();

        CHECK(nextSelectionId != std::numeric_limits<uint32_t>::max(), "scene selection IDs exhausted");
        actor->id = nextSelectionId++;
    }

    data = std::move(loaded);
    ++generation;
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
    const auto  type  = types.find(std::string{typeName});
    CHECK(type != types.end(), "unknown actor type: {}", typeName);

    auto     actor = type->second.Create();
    uint64_t index = 1;
    do
    {
        actor->name = std::string{typeName} + std::to_string(index++);
    }
    while (std::ranges::any_of(data.actors, [&](const auto& existing) { return existing->name == actor->name; }));

    actor->init();
    CHECK(nextSelectionId != std::numeric_limits<uint32_t>::max(), "scene selection IDs exhausted");
    actor->id    = nextSelectionId++;
    auto& result = *actor;
    data.actors.push_back(std::move(actor));

    return result;
}

void SceneManager::deleteActor(uint32_t selectionId)
{
    DCHECK(inited);
    const auto found =
        std::ranges::find_if(data.actors, [=](const auto& actor) { return actor->selectionId() == selectionId; });
    if (found == data.actors.end())
        return;

    data.actors.erase(found);
}

void SceneManager::shutdown() noexcept
{
    data = {};
    DEBUG_EXEC(inited = false);
}

void SceneManager::tick(float deltaTime)
{
    for (const auto& actor : data.actors)
        actor->tick(deltaTime);
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
