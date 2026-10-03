#pragma once

#include "asset/Asset.h"
#include "scene/actor/FreeFlyCameraActor.h"

#include <cstdint>
#include <string_view>

class SceneManager
{
public:
    ~SceneManager();
    static constexpr std::string_view editorCameraName = "Editor Camera";

    void load(const SceneAsset::ID& id);
    void load(const SceneAsset& scene);
    void save();
    void shutdown() noexcept;
    void tick(float deltaTime);

    [[nodiscard]] SceneAsset& scene() noexcept;
    [[nodiscard]] const SceneAsset& scene() const noexcept;
    [[nodiscard]] FreeFlyCameraActor& editorCamera() const;
    [[nodiscard]] Actor* findActor(uint32_t selectionId) const noexcept;

private:
    SceneAsset data;
    FreeFlyCameraActor* cameraActor = nullptr;
    uint32_t nextSelectionId = 1;
};
