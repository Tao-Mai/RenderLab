#pragma once

#include "scene/Scene.h"

#include <cstdint>
#include <string_view>

class SceneManager
{
public:
    ~SceneManager();

    void                  init();
    void                  loadDefaultScene();
    void                  load(const SceneAsset::ID& id);
    void                  load(const SceneAsset& scene);
    void                  save();
    void                  shutdown() noexcept;
    void                  tick(float deltaTime);
    [[nodiscard]] AActor& createActor(std::string_view typeName);
    void                  deleteActor(uint32_t selectionId);

    [[nodiscard]] SceneAsset&       scene() noexcept { return mpScene->getAsset(); }
    [[nodiscard]] const SceneAsset& scene() const noexcept { return std::as_const(*mpScene).getAsset(); }
    [[nodiscard]] const Falcor::ref<Falcor::Scene>& getScene() const { return mpScene; }
    [[nodiscard]] uint64_t                          sceneGeneration() const { return generation; }
    [[nodiscard]] AActor*                           findActor(uint32_t selectionId) const noexcept;

private:
    Falcor::ref<Falcor::Scene> mpScene;
    uint64_t                   generation      = 0;
    uint32_t                   nextSelectionId = 1;
};
