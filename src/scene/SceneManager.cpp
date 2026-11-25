#include "scene/SceneManager.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Logger.h"

void SceneManager::loadDefaultScene() { load(context().config.initialScene); }

void SceneManager::load(const Scene::ID& id) { load(context().assetManager->get(id)); }

void SceneManager::load(const Scene& scene)
{
    CHECK(!scene.environmentMap || !scene.environmentMap->empty());
    for (const auto& object : scene.objects)
        CHECK(!object.meshId.empty(), "object '{}' requires a mesh", object.name);

    activeScene = scene;
    ++generation;
}

void SceneManager::shutdown() noexcept { activeScene = {}; }
