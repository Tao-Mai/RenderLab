#include "scene/SceneManager.h"

#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Serializer.h"
#include "metadata/RegisteredTypes.generated.h"

#include <algorithm>
#include <limits>

SceneManager::~SceneManager() {}

void SceneManager::init()
{


    static const bool registered = []
    {
        metadata::visitRegisteredTypes<Component>([]<class T>(const char*) { RegisterType<T, Component>(); });
        metadata::visitRegisteredTypes<AActor>([]<class T>(const char*) { RegisterType<T, AActor>(); });
        return true;
    }();

    mpScene = Falcor::make_ref<Falcor::Scene>();
}

void SceneManager::loadDefaultScene() { load(context().config->initialScene()); }

void SceneManager::load(const SceneAsset::ID& id) { load(context().assetManager->get<SceneAsset>(id)); }

void SceneManager::load(const SceneAsset& scene)
{


    // The cache owns a snapshot; the live scene owns its actors and components.
    SceneAsset loaded;
    Deserialize(Serialize(scene), loaded);
    CHECK(!loaded.environmentMap || !loaded.environmentMap->empty());
    CHECK(
        !loaded.editorCamera.exposure || loaded.editorCamera.exposure->isValid(),
        "invalid camera exposure in scene '{}'",
        loaded.id
    );
    for (const auto& actor : loaded.actors)
    {
        CHECK(actor);
        actor->init();

        CHECK(nextSelectionId != std::numeric_limits<uint32_t>::max(), "scene selection IDs exhausted");
        actor->id = nextSelectionId++;
    }

    mpScene = Falcor::make_ref<Falcor::Scene>(std::move(loaded));
    ++generation;
}

void SceneManager::save()
{


    SceneAsset snapshot;
    Deserialize(Serialize(scene()), snapshot);
    context().assetManager->save<SceneAsset>(std::move(snapshot));
}

AActor& SceneManager::createActor(std::string_view typeName)
{

    const auto& types = TypeRegistry<AActor>::get().entries;
    const auto  type  = types.find(std::string{typeName});
    CHECK(type != types.end(), "unknown actor type: {}", typeName);

    auto&    actors = scene().actors;
    auto     actor  = type->second.create();
    uint64_t index  = 1;
    do
    {
        actor->name = std::string{typeName} + std::to_string(index++);
    }
    while (std::ranges::any_of(actors, [&](const auto& existing) { return existing->name == actor->name; }));

    actor->init();
    CHECK(nextSelectionId != std::numeric_limits<uint32_t>::max(), "scene selection IDs exhausted");
    actor->id    = nextSelectionId++;
    auto& result = *actor;
    actors.push_back(std::move(actor));

    return result;
}

void SceneManager::deleteActor(uint32_t selectionId)
{

    auto&      actors = scene().actors;
    const auto found =
        std::ranges::find_if(actors, [=](const auto& actor) { return actor->selectionId() == selectionId; });
    if (found == actors.end())
        return;

    actors.erase(found);
}

void SceneManager::shutdown() noexcept { mpScene.reset(); }

void SceneManager::tick(float deltaTime)
{
    for (const auto& actor : scene().actors)
        actor->tick(deltaTime);
}

AActor* SceneManager::findActor(uint32_t selectionId) const noexcept
{
    if (selectionId == 0)
        return nullptr;
    for (const auto& actor : scene().actors)
        if (actor->selectionId() == selectionId)
            return actor.get();

    return nullptr;
}
