#pragma once

#include "editor/EditorUi.h"
#include "render/Renderer.h"
#include "core/Debug.h"

class CameraComponent;

class Editor
{
public:
    Editor() = default;
    ~Editor();

    Editor(const Editor&)            = delete;
    Editor& operator=(const Editor&) = delete;

    void init();
    void refreshUi();
    void shutdown() noexcept;

    [[nodiscard]] EditorFrameInput buildFrame(
        const CameraComponent& camera,
        float deltaTime,
        uint32_t swapchainWidth,
        uint32_t swapchainHeight);
    void applyPickResult(const EditorFrameResult& result);

    [[nodiscard]] bool wantsInput() const;

private:
    DEBUG_ONLY(bool inited = false;)
    EditorUI ui;
    uint32_t selectedId = 0;
    bool dirty = false;

    void saveScene();
};
