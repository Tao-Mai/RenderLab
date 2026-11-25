#pragma once

#include "scene/Scene.h"

#include <cstdint>

class SceneManager
{
public:
    void loadDefaultScene();
    void load(const Scene::ID& id);
    void load(const Scene& scene);
    void shutdown() noexcept;

    [[nodiscard]] Scene& scene() noexcept { return activeScene; }
    [[nodiscard]] const Scene& scene() const noexcept { return activeScene; }
    [[nodiscard]] uint64_t sceneGeneration() const { return generation; }

private:
    Scene    activeScene;
    uint64_t generation = 0;
};
