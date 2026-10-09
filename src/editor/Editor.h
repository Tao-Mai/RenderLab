#pragma once

#include "core/Debug.h"
#include "editor/EditorUi.h"
#include "render/RenderView.h"
#include "render/feature/editor/EditorFrameInput.h"

class EditorCamera;

class Editor
{
public:
    struct Frame
    {
        RenderView       view;
        EditorFrameInput input;
        bool             rebuildGraph = false;
    };

    Editor() = default;
    ~Editor();

    Editor(const Editor&)            = delete;
    Editor& operator=(const Editor&) = delete;

    void init();
    void refreshUi();
    void shutdown() noexcept;
    void applySceneActions();

    [[nodiscard]] Frame buildFrame(
        const EditorCamera& camera,
        float deltaTime,
        uint32_t swapchainWidth,
        uint32_t swapchainHeight
    );
    void applyPickResult(const RenderFrameResult& result);

    [[nodiscard]] bool     wantsInput() const;
    [[nodiscard]] uint32_t selectionId() const { return selectedId; }

private:
    DEBUG_ONLY(bool inited = false;)
    EditorUI                  ui;
    LightMarkers              lightMarkers;
    uint32_t                  selectedId = 0;
    bool                      dirty      = false;
    EditorUI::HierarchyResult pendingSceneActions;

    void saveScene();
};
