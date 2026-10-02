#pragma once

#include "asset/Asset.h"
#include "scene/actor/FreeFlyCameraActor.h"

#include <cstdint>
#include <string_view>

class SceneManager
{
public:
    static constexpr std::string_view editorCameraName = "Editor Camera";

    void load(const Scene::ID& id);
    void load(const Scene::Desc& scene);
    void save();
    void reset() noexcept;
    bool tick(float deltaTime);

    [[nodiscard]] Scene::Desc& scene() noexcept;
    [[nodiscard]] const Scene::Desc& scene() const noexcept;
    [[nodiscard]] FreeFlyCameraActor& editorCamera() const;
    [[nodiscard]] Actor* findActor(uint32_t selectionId) const noexcept;

private:
    Scene::Desc data;
    FreeFlyCameraActor* cameraActor = nullptr;
    uint32_t nextSelectionId = 1;
};
