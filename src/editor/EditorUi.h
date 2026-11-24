#pragma once

#include "asset/SceneAsset.h"
#include "editor/SceneHierarchy.h"
#include "render/ViewportRect.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

struct GLFWwindow;
struct TransformComponent;

class EditorUI
{
public:
    struct InspectorResult
    {
        std::optional<SceneAsset::ID> sceneId;
        bool                          edited = false;
    };

    EditorUI() = default;

    EditorUI(const EditorUI&)            = delete;
    EditorUI& operator=(const EditorUI&) = delete;

    void               beginFrame(bool showObjectList);
    [[nodiscard]] int  objectListInset(int windowWidth);
    SceneObjectActions drawHierarchy(SceneAsset& scene, uint32_t& selectedId)
    {
        return hierarchyDockId ? hierarchy.draw(scene, selectedId, hierarchyDockId) : SceneObjectActions{};
    }
    [[nodiscard]] bool
    drawGizmo(TransformComponent& transform, const glm::mat4& view, const glm::mat4& projection, bool enableShortcuts);
    [[nodiscard]] InspectorResult drawInspector(SceneAsset& scene, AActor* object, bool dirty);
    void                          endFrame();
    void                          resetLayout();

    [[nodiscard]] bool         sceneClicked(glm::vec2& viewportUv) const;
    [[nodiscard]] bool         wantsInput() const;
    [[nodiscard]] bool         saveRequested() const;
    [[nodiscard]] float        sceneAspectRatio() const;
    [[nodiscard]] ViewportRect sceneViewportPixels() const { return scenePixels; }

private:
    uint32_t       editorDockId    = 0;
    uint32_t       hierarchyDockId = 0;
    float          hierarchyWidth  = 0.0f;
    float          editorWidth     = 0.0f;
    float          centerWidth     = 0.0f;
    int            gizmoOperation  = 0;
    glm::vec2      scenePosition{0.0f};
    glm::vec2      sceneSize{1.0f};
    ViewportRect   scenePixels;
    SceneHierarchy hierarchy;
};
