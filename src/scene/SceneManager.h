#pragma once

#include "scene/SceneLayer.h"

#include <cstddef>
#include <cstdint>

class SceneManager
{
public:
    void loadDefaultScene();
    void load(const Scene::ID& id);
    void load(const Scene& scene);
    void setLayer(SceneLayer::ID id);
    void shutdown() noexcept;

    [[nodiscard]] Scene& scene() noexcept { return activeScene; }
    [[nodiscard]] const Scene& scene() const noexcept { return activeScene; }
    [[nodiscard]] const SceneLayer::ID& layerId() const noexcept { return activeLayer; }
    [[nodiscard]] uint64_t sceneGeneration() const { return generation; }

private:
    void applyLayer(SceneLayer::ID id);

    Scene activeScene;
    SceneLayer::ID activeLayer;

    std::size_t baseObjectCount = 0;
    std::size_t baseLightCount = 0;

    uint64_t generation = 0;
};
