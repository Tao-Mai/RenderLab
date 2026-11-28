#pragma once

#include "core/utils/NonCopyable.h"
#include "editor/EditorBackend.h"
#include "editor/EditorCamera.h"
#include "editor/EditorUi.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <slang-rhi.h>

class Editor : private NonCopyable
{
public:
    struct Frame
    {
        rhi::Viewport viewport;
        std::function<void(rhi::IRenderPassEncoder&, uint32_t)> recordUi;
    };

    Editor() = default;
    ~Editor();

    void init();
    void refreshUi();
    [[nodiscard]] bool resizeWindowForLayout();
    void tick(float deltaTime);
    EditorCamera& camera() { return *editorCamera; }
    void shutdown() noexcept;
    void applySceneSwitch();

    [[nodiscard]] Frame buildFrame();

private:
    std::unique_ptr<EditorCamera> editorCamera;
    EditorBackend backend;
    EditorUI ui;
    bool windowHasObjectList = false;
    std::optional<Scene::ID> pendingSceneId;

#ifdef _WIN32
    void* previousImeContext = nullptr;
#endif

    void initBackend();
};
