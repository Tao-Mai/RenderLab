#pragma once

#include "asset/Asset.h"
#include "editor/EditorCamera.h"

#include <cstdint>
#include <string_view>

class SceneManager
{
public:
    ~SceneManager();

    void init();
    void loadDefaultScene();
    void load(const SceneAsset::ID& id);
    void load(const SceneAsset& scene);
    void save();
    void shutdown() noexcept;
    void tick(float deltaTime);
    [[nodiscard]] AActor& createActor(std::string_view typeName);
    void deleteActor(uint32_t selectionId);

    [[nodiscard]] SceneAsset& scene() noexcept { return data; }
    [[nodiscard]] const SceneAsset& scene() const noexcept { return data; }
    [[nodiscard]] EditorCamera& editorCamera() { return camera; }
    [[nodiscard]] const EditorCamera& editorCamera() const { return camera; }
    [[nodiscard]] AActor* findActor(uint32_t selectionId) const noexcept;

private:
    DEBUG_ONLY(bool inited = false;)
    SceneAsset data;
    EditorCamera camera{data.editorCamera};
    uint32_t nextSelectionId = 1;
};
