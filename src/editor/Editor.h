#pragma once

#include "editor/EditorUi.h"
#include "render/Renderer.h"

class CameraComponent;

class Editor
{
public:
    Editor() = default;
    ~Editor() = default;

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
    void markDirty();

    [[nodiscard]] bool wantsInput() const;

private:
    EditorUI ui;
    uint32_t selectedId = 0;
    bool dirty = false;

    void saveScene();
};
