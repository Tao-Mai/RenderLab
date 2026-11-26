#include "scene/SceneManager.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <utility>

void SceneManager::loadDefaultScene() { load(context().config.initialScene); }

void SceneManager::load(const Scene::ID& id) { load(context().assetManager->get(id)); }

void SceneManager::load(const Scene& scene)
{
    CHECK(!scene.environmentMap || !scene.environmentMap->empty());
    CHECK(!scene.defaultLayer || !scene.defaultLayer->empty(), "empty default layer ID for scene '{}'", scene.id);
    for (const auto& object : scene.objects)
        CHECK(!object.meshId.empty(), "object '{}' requires a mesh", object.name);

    activeScene = scene;
    activeLayer = {};
    baseObjectCount = scene.objects.size();
    baseLightCount = scene.analyticLights.size();

    if (activeScene.defaultLayer)
        setLayer(*activeScene.defaultLayer);

    ++generation;
}

void SceneManager::setLayer(SceneLayer::ID id)
{
    if (activeLayer == id)
        return;

    const auto* layer = id.empty() ? nullptr : &context().assetManager->get(id);
    if (layer)
    {
        CHECK(layer->baseScene == activeScene.id, "scene layer '{}' targets '{}'", layer->id, layer->baseScene);
        for (const auto& object : layer->objects)
            CHECK(!object.meshId.empty(), "object '{}' in scene layer '{}' requires a mesh", object.name, id);
    }

    // The base scene occupies the prefix of each list.
    activeScene.objects.resize(baseObjectCount);
    activeScene.analyticLights.resize(baseLightCount);
    if (layer)
    {
        activeScene.objects.insert(activeScene.objects.end(), layer->objects.begin(), layer->objects.end());
        activeScene.analyticLights.insert(
            activeScene.analyticLights.end(), layer->analyticLights.begin(), layer->analyticLights.end()
        );
    }
    activeLayer = std::move(id);
}

void SceneManager::shutdown() noexcept
{
    activeScene = {};
    activeLayer = {};
    baseObjectCount = 0;
    baseLightCount = 0;
}
