#pragma once

#include "asset/SceneAsset.h"
#include "editor/AssetImportPanel.h"
#include "editor/SceneHierarchy.h"
#include "render/ViewportRect.h"

#include <cstdint>
#include <optional>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <vulkan/vulkan.h>

struct GLFWwindow;
struct TransformComponent;

class EditorUI
{
public:
    struct InspectorResult
    {
        bool edited = false;
    };

    EditorUI() = default;

    EditorUI(const EditorUI&)            = delete;
    EditorUI& operator=(const EditorUI&) = delete;

    void         beginFrame(float deltaTime);
    SceneActions drawHierarchy(SceneAsset& scene, uint32_t& selectedId)
    {
        return hierarchy.draw(scene, selectedId, hierarchyDockId);
    }
    [[nodiscard]] bool
    drawGizmo(TransformComponent& transform, const glm::mat4& view, const glm::mat4& projection, bool enableShortcuts);
    [[nodiscard]] InspectorResult drawInspector(SceneAsset& scene, AActor* object, bool dirty);
    void                          endFrame();
    void                          shutdown() noexcept
    {
        imports.shutdown();
        resetLayout();
    }
    void resetLayout();

    [[nodiscard]] bool         sceneClicked(glm::vec2& mousePosition) const;
    [[nodiscard]] bool         wantsInput() const;
    [[nodiscard]] bool         saveRequested() const;
    [[nodiscard]] float        sceneAspectRatio() const;
    [[nodiscard]] ViewportRect sceneViewportPixels() const { return scenePixels; }

private:
    bool             dockLayoutInitialized = false;
    uint32_t         editorDockId          = 0;
    uint32_t         hierarchyDockId       = 0;
    int              gizmoOperation        = 0;
    glm::vec2        scenePosition{0.0f};
    glm::vec2        sceneSize{1.0f};
    ViewportRect     scenePixels;
    SceneHierarchy   hierarchy;
    AssetImportPanel imports;
};
