#pragma once

#include "core/utils/NonCopyable.h"
#include "scene/Scene.h"

#include <cstdint>
#include <optional>
#include <slang-rhi.h>

class EditorUI : private NonCopyable
{
public:
    EditorUI() = default;
    EditorUI(EditorUI&&) = delete;

    void beginFrame(bool showObjectList);
    [[nodiscard]] int objectListInset(int windowWidth);
    void drawSceneList(const Scene& scene) const;
    [[nodiscard]] std::optional<Scene::ID> drawControls(const Scene& scene);
    void endFrame();
    void resetLayout();

    [[nodiscard]] bool wantsInput() const;
    [[nodiscard]] rhi::Viewport sceneViewportPixels() const { return scenePixels; }

private:
    uint32_t      editorDockId    = 0;
    uint32_t      hierarchyDockId = 0;
    float         hierarchyWidth  = 0.0f;
    float         editorWidth     = 0.0f;
    float         centerWidth     = 0.0f;
    rhi::Viewport scenePixels     = rhi::Viewport::fromSize(1.0f, 1.0f);

    double   fpsElapsedSeconds = 0.0;
    uint32_t fpsFrameCount      = 0;
    float    displayedFps       = 0.0f;
};
