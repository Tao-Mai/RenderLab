#pragma once

#include "editor/editor_ui.h"
#include "render/renderer.h"

class Camera;
class GpuScene;

class Editor
{
public:
    Editor() = default;
    ~Editor() = default;

    Editor(const Editor&)            = delete;
    Editor& operator=(const Editor&) = delete;

    void init();
    void shutdown() noexcept;

    [[nodiscard]] EditorFrameInput buildFrame(
        const Camera& camera,
        float deltaTime,
        uint32_t swapchainWidth,
        uint32_t swapchainHeight);
    void applyPickResult(const EditorFrameResult& result, const GpuScene& scene);

    [[nodiscard]] bool wantsInput() const;

private:
    EditorUI ui;
    SceneObjectDesc* selectedObject = nullptr;
    Light* selectedLight = nullptr;
};
