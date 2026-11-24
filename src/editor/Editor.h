#pragma once

#include <memory>
#include <optional>
#include "editor/EditorBackend.h"
#include "editor/EditorCamera.h"
#include "editor/EditorUi.h"
#include "render/RenderView.h"
#include "render/editor/EditorFrameInput.h"

class EditorCamera;

class Editor
{
public:
    struct Frame
    {
        RenderView       view;
        EditorFrameInput input;
    };

    Editor() = default;
    ~Editor();

    Editor(const Editor&)            = delete;
    Editor& operator=(const Editor&) = delete;

    void                init();
    void                refreshUi();
    [[nodiscard]] bool  resizeWindowForLayout();
    void                tick(float deltaTime);
    EditorCamera&       camera() { return *editorCamera; }
    const EditorCamera& camera() const { return *editorCamera; }
    void                shutdown() noexcept;
    void                applySceneActions();

    [[nodiscard]] Frame
         buildFrame(const EditorCamera& camera, float deltaTime, uint32_t swapchainWidth, uint32_t swapchainHeight);
    void applyPickResult(const RenderFrameResult& result);

    [[nodiscard]] bool     wantsInput() const;
    [[nodiscard]] uint32_t selectionId() const { return selectedId; }

private:
    std::unique_ptr<EditorCamera> editorCamera;
    uint64_t                      sceneGeneration = 0;
    EditorBackend                 backend;
    EditorUI                      ui;
    uint32_t                      selectedId          = 0;
    bool                          dirty               = false;
    bool                          windowHasObjectList = false;
    std::optional<SceneAsset::ID> pendingSceneId;
    SceneObjectActions            pendingObjectActions;

    void saveScene();
    void initBackend();
};
