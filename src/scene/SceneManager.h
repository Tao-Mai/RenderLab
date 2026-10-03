#pragma once

#include "asset/Asset.h"
#include "scene/actor/AFreeFlyCamera.h"

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
    [[nodiscard]] AFreeFlyCamera& editorCamera() const;
    [[nodiscard]] AActor* findActor(uint32_t selectionId) const noexcept;

private:
    SceneAsset data;
    AFreeFlyCamera* cameraActor = nullptr;
    uint32_t nextSelectionId = 1;
};
