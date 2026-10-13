#include "editor/EditorUi.h"
#include "editor/PerformancePanel.h"

#include <imgui.h>

// ImGuizmo requires ImGui types to be declared before its header.
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <utility>
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "editor/Inspector.h"
#include "editor/RendererSettings.h"
#include "render/Renderer.h"
#include "scene/component/TransformComponent.h"

namespace
{
constexpr float kEditorWidthRatio = 0.24f;
constexpr float kHierarchyWidthRatio = 0.23f;

void applyModelMatrix(TransformComponent& transform, const glm::mat4& model)
{
    transform.position = glm::vec3{model[3]};

    glm::vec3 scale{
        glm::length(glm::vec3{model[0]}),
        glm::length(glm::vec3{model[1]}),
        glm::length(glm::vec3{model[2]}),
    };
    scale = glm::max(scale, glm::vec3{0.001f});

    glm::mat3 rotationMatrix{
        glm::vec3{model[0]} / scale.x,
        glm::vec3{model[1]} / scale.y,
        glm::vec3{model[2]} / scale.z,
    };
    glm::quat rotation = glm::normalize(glm::quat_cast(rotationMatrix));

    // q and -q are the same rotation. Keeping the closest representation
    // prevents the editor value from flipping sign during a gizmo drag.
    if (glm::dot(rotation, transform.rotation) < 0.0f)
    {
        rotation = -rotation;
    }

    transform.rotation = rotation;
    transform.scale    = scale;
}
}  // namespace
void EditorUI::beginFrame(float)
{
    ImGuizmo::BeginFrame();

    const ImGuiViewport*     viewport = ImGui::GetMainViewport();
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 framebufferScale{io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y};
    DCHECK(framebufferScale.x > 0.0f && framebufferScale.y > 0.0f);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float separator = style.DockingSeparatorSize;
    const auto fitPanelWidths = [&](float hierarchyWidth, float editorWidth)
    {
        const float availableWidth = std::max(viewport->WorkSize.x - 2.0f * separator, 1.0f);
        const float minimumWidth = std::min(style.WindowMinSize.x, availableWidth / 3.0f);
        const float squareWidth = std::min(
            std::floor(viewport->WorkSize.y * framebufferScale.y / framebufferScale.x),
            availableWidth - 2.0f * minimumWidth
        );
        const float panelWidth = availableWidth - squareWidth;
        const float adjustment = (panelWidth - hierarchyWidth - editorWidth) * 0.5f;
        hierarchyWidth = std::floor(std::clamp(hierarchyWidth + adjustment, minimumWidth, panelWidth - minimumWidth));
        return std::pair{hierarchyWidth, panelWidth - hierarchyWidth};
    };

    // Apply explicit panel widths before DockSpace performs its layout, so the center uses the square remainder.
    if (dockLayoutInitialized)
    {
        const auto* hierarchyNode = ImGui::DockBuilderGetNode(hierarchyDockId);
        const auto* editorNode = ImGui::DockBuilderGetNode(editorDockId);
        DCHECK(hierarchyNode && editorNode);
        const auto [hierarchyWidth, editorWidth] = fitPanelWidths(hierarchyNode->SizeRef.x, editorNode->SizeRef.x);
        ImGui::DockBuilderSetNodeSize(hierarchyDockId, {hierarchyWidth, viewport->WorkSize.y});
        ImGui::DockBuilderSetNodeSize(editorDockId, {editorWidth, viewport->WorkSize.y});
    }

    const ImGuiDockNodeFlags dockFlags =
        ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode;
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, viewport, dockFlags);

    if (!dockLayoutInitialized)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(
            dockspaceId,
            static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) | ImGuiDockNodeFlags_PassthruCentralNode
        );
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        const float defaultEditorWidth = viewport->WorkSize.x * kEditorWidthRatio;
        const auto [hierarchyWidth, editorWidth] = fitPanelWidths(
            (viewport->WorkSize.x - defaultEditorWidth - separator) * kHierarchyWidthRatio,
            defaultEditorWidth
        );
        ImGuiID centerDockId = dockspaceId;
        editorDockId = ImGui::DockBuilderSplitNode(
            centerDockId, ImGuiDir_Right, editorWidth / (viewport->WorkSize.x - separator), nullptr, &centerDockId
        );
        ImGui::DockBuilderSetNodeSize(editorDockId, {editorWidth, viewport->WorkSize.y});
        ImGui::DockBuilderDockWindow("Editor", editorDockId);
        const float centerWidth = viewport->WorkSize.x - editorWidth - separator;
        ImGui::DockBuilderSetNodeSize(centerDockId, {centerWidth, viewport->WorkSize.y});
        hierarchyDockId = ImGui::DockBuilderSplitNode(
            centerDockId, ImGuiDir_Left, hierarchyWidth / (centerWidth - separator), nullptr, &centerDockId
        );
        ImGui::DockBuilderSetNodeSize(hierarchyDockId, {hierarchyWidth, viewport->WorkSize.y});
        ImGui::DockBuilderDockWindow("Scene Objects", hierarchyDockId);
        ImGui::DockBuilderFinish(dockspaceId);
        dockLayoutInitialized = true;
    }

    if (const ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId))
    {
        const glm::vec2 availablePixels{
            std::max(centralNode->Size.x, 1.0f) * framebufferScale.x,
            std::max(centralNode->Size.y, 1.0f) * framebufferScale.y,
        };
        const float side = std::max(1.0f, std::floor(std::min(availablePixels.x, availablePixels.y)));
        // Fit and center a pixel-aligned square; all render paths consume this same viewport.
        scenePixels = {
            .x = std::floor((centralNode->Pos.x - viewport->Pos.x) * framebufferScale.x +
                            (availablePixels.x - side) * 0.5f),
            .y = std::floor((centralNode->Pos.y - viewport->Pos.y) * framebufferScale.y +
                            (availablePixels.y - side) * 0.5f),
            .width = side,
            .height = side,
        };

        // ImGui, picking and ImGuizmo use screen coordinates, including framebuffer scaling.
        scenePosition = {
            viewport->Pos.x + scenePixels.x / framebufferScale.x,
            viewport->Pos.y + scenePixels.y / framebufferScale.y,
        };
        sceneSize = {side / framebufferScale.x, side / framebufferScale.y};
    }

    // Focus follows mouse clicks, not hover, so moving into a panel does not hide the gizmo.
    for (int button = 0; button < ImGuiMouseButton_COUNT; ++button)
    {
        if (!ImGui::IsMouseClicked(button))
            continue;

        sceneFocused = ImGui::IsMouseHoveringRect(
            {scenePosition.x, scenePosition.y},
            {scenePosition.x + sceneSize.x, scenePosition.y + sceneSize.y},
            false
        ) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
        break;
    }
}
bool EditorUI::drawGizmo(
    TransformComponent& transform, const glm::mat4& view, const glm::mat4& projection, bool enableShortcuts
)
{
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        return false;

    const ImGuiIO& io = ImGui::GetIO();
    if (enableShortcuts && !io.WantTextInput && !ImGui::IsAnyItemActive())
    {
        if (context().inputManager->get(Command::GizmoTranslate))
        {
            gizmoOperation = 0;
        }
        else if (context().inputManager->get(Command::GizmoRotate))
        {
            gizmoOperation = 1;
        }
        else if (context().inputManager->get(Command::GizmoScale))
        {
            gizmoOperation = 2;
        }
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::AllowAxisFlip(false);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImGuizmo::SetDrawlist(drawList);
    ImGuizmo::SetRect(scenePosition.x, scenePosition.y, sceneSize.x, sceneSize.y);

    glm::mat4                     model        = transform.matrix();
    constexpr ImGuizmo::OPERATION operations[] = {
        ImGuizmo::TRANSLATE,
        ImGuizmo::ROTATE,
        ImGuizmo::SCALE,
    };
    const ImGuizmo::OPERATION operation = operations[std::clamp(gizmoOperation, 0, 2)];
    drawList->PushClipRect(
        {scenePosition.x, scenePosition.y},
        {scenePosition.x + sceneSize.x, scenePosition.y + sceneSize.y},
        true
    );
    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        operation,
        ImGuizmo::LOCAL,
        glm::value_ptr(model)
    );
    drawList->PopClipRect();

    if (ImGuizmo::IsUsing())
    {
        applyModelMatrix(transform, model);
        return true;
    }
    return false;
}
EditorUI::InspectorResult EditorUI::drawInspector(SceneAsset& scene, AActor* object, bool dirty)
{
    ImGui::SetNextWindowDockID(editorDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags editorFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Editor", nullptr, editorFlags);

    drawPerformance(context().profiler);
    const std::string& sceneName = scene.id.value;
    if (dirty)
    {
        ImGui::Text("%.*s*", static_cast<int>(sceneName.size()), sceneName.data());
    }
    else
    {
        ImGui::TextUnformatted(sceneName.data(), sceneName.data() + sceneName.size());
    }

    InspectorResult result;
    result.edited |= drawCameraInspector(scene.editorCamera);

    result.edited |= drawRendererSettings(
        *context().config,
        *context().assetManager,
        context().renderer->vulkanContext(),
        scene
    );
    imports.draw();

    result.edited |= drawObjectInspector(object);

    ImGui::End();
    return result;
}
void EditorUI::endFrame() { ImGui::Render(); }
bool EditorUI::sceneClicked(glm::vec2& mousePosition) const
{
    const ImVec2 mouse  = ImGui::GetMousePos();
    const bool   inside = mouse.x >= scenePosition.x && mouse.y >= scenePosition.y &&
        mouse.x < scenePosition.x + sceneSize.x && mouse.y < scenePosition.y + sceneSize.y;
    if (!inside ||
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
        ImGui::IsAnyItemActive() || !context().inputManager->get(Command::SelectObject) ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || ImGuizmo::IsOver() ||
        ImGuizmo::IsUsing())
    {
        return false;
    }
    mousePosition = {
        ((mouse.x - scenePosition.x) / sceneSize.x) * 2.0f - 1.0f,
        ((mouse.y - scenePosition.y) / sceneSize.y) * 2.0f - 1.0f,
    };
    return true;
}

bool EditorUI::wantsInput() const
{
    if (!ImGui::GetCurrentContext())
    {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

bool EditorUI::saveRequested() const
{
    if (!ImGui::GetCurrentContext())
    {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return false;
    }
    return context().inputManager->get(Command::SaveScene);
}

float EditorUI::sceneAspectRatio() const { return scenePixels.width / scenePixels.height; }


void EditorUI::resetLayout()
{
    dockLayoutInitialized = false;
    sceneFocused          = false;
    editorDockId          = 0;
    hierarchyDockId       = 0;
    hierarchy.reset();
}
