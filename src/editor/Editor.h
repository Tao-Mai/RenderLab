#pragma once
#include "editor/LightMarkers.h"

#include <memory>
#include "core/Debug.h"
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
    DEBUG_ONLY(bool inited = false;)
    std::unique_ptr<EditorCamera> editorCamera;
    uint64_t                      sceneGeneration = 0;
    EditorBackend                 backend;
    EditorUI                      ui;
    LightMarkers                  lightMarkers;
    uint32_t                      selectedId = 0;
    bool                          dirty      = false;
    SceneActions                  pendingSceneActions;

    void saveScene();
    void initBackend();
};
