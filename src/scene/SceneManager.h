#pragma once

#include "asset/SceneAsset.h"

#include <cstdint>
#include <string_view>

class SceneManager
{
public:
    void                  init();
    void                  loadDefaultScene();
    void                  load(const SceneAsset::ID& id);
    void                  load(const SceneAsset& scene);
    void                  save();
    void                  shutdown() noexcept;
    void                  tick(float deltaTime);
    [[nodiscard]] AActor& createActor(std::string_view typeName);
    void                  deleteActor(uint32_t selectionId);

    [[nodiscard]] SceneAsset&       scene() noexcept { return activeScene; }
    [[nodiscard]] const SceneAsset& scene() const noexcept { return activeScene; }
    [[nodiscard]] uint64_t          sceneGeneration() const { return generation; }
    [[nodiscard]] AActor*           findActor(uint32_t selectionId) const noexcept;

private:
    SceneAsset activeScene;
    uint64_t   generation      = 0;
    uint32_t   nextSelectionId = 1;
};
