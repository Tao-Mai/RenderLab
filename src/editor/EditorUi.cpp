#include "editor/EditorUi.h"
#include "editor/PassEditor.h"
#include "editor/PerformancePanel.h"

#include <imgui.h>

// ImGuizmo requires ImGui types to be declared before its header.
#include <ImGuizmo.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>
#include <utility>
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "editor/Inspector.h"
#include "render/Renderer.h"
#include "scene/component/TransformComponent.h"

namespace
{
constexpr float kEditorWidthRatio = 0.24f;
constexpr float kHierarchyWidthRatio = 0.23f;
constexpr float kMinimumEditableScale = 0.001f;

std::optional<SceneAsset::ID> drawSceneSelector(const SceneAsset::ID& current, bool dirty)
{
    std::optional<SceneAsset::ID> chosen;
    if (!ImGui::BeginMenuBar())
        return chosen;

    const std::string preview = dirty ? current.value + "*" : current.value;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##Scene", preview.c_str()))
    {
        DCHECK(context().assetManager);
        auto sceneIds = context().assetManager->loadedIds<SceneAsset>();
        std::ranges::sort(sceneIds, {}, &SceneAsset::ID::value);
        for (const auto& id : sceneIds)
        {
            const bool selected = id == current;
            if (ImGui::Selectable(id.c_str(), selected) && !selected)
                chosen = id;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Switch scenes and discard unsaved scene changes");
    ImGui::EndMenuBar();
    return chosen;
}

void drawRenderGraphSelector(ConfigManager& config, bool supportsRayTracing)
{
    const auto current = config.renderGraphName();
    const bool currentOffline = config.rendererConfig().renderPath == RenderPath::OfflinePathTracing;
    if (!ImGui::BeginCombo("Render graph", current.c_str()))
        return;

    for (const auto& [path, scripts] : config.renderGraphs())
    {
        const bool offline = path == RenderPath::OfflinePathTracing;
        if (offline != currentOffline)
            continue;

        ImGui::BeginDisabled(requiresRayTracing(path) && !supportsRayTracing);
        for (const auto& [name, script] : scripts)
        {
            const bool selected = name == current;
            if (ImGui::Selectable(name.c_str(), selected) && !selected)
                config.setRenderGraph(name);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndDisabled();
    }
    ImGui::EndCombo();
}

void applyModelMatrix(TransformComponent& transform, const glm::mat4& model)
{
    TransformComponent edited;
    CHECK(edited.setMatrix(model));
    DCHECK(edited.base == glm::mat4{1});
    for (glm::length_t axis = 0; axis < 3; ++axis)
        edited.scale[axis] = std::copysign(std::max(std::abs(edited.scale[axis]), kMinimumEditableScale),
                                         edited.scale[axis]);
    glm::quat rotation = edited.rotation;

    // q and -q are the same rotation. Keeping the closest representation
    // prevents the editor value from flipping sign during a gizmo drag.
    if (glm::dot(rotation, transform.rotation) < 0.0f)
    {
        rotation = -rotation;
    }

    transform.position = edited.position;
    transform.rotation = rotation;
    transform.scale    = edited.scale;
}
}  // namespace

int EditorUI::objectListInset(int windowWidth)
{
    if (hierarchyWidth == 0.0f)
        hierarchyWidth = std::floor(static_cast<float>(windowWidth) * kHierarchyWidthRatio);
    return static_cast<int>(std::lround(hierarchyWidth + ImGui::GetStyle().DockingSeparatorSize));
}

void EditorUI::beginFrame(bool showObjectList)
{
    ImGuizmo::BeginFrame();

    const ImGuiViewport*     viewport = ImGui::GetMainViewport();
    const ImGuiIO& io = ImGui::GetIO();
    const glm::vec2 framebufferScale{io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y};
    DCHECK(framebufferScale.x > 0.0f && framebufferScale.y > 0.0f);
    const ImGuiStyle& style = ImGui::GetStyle();
    const float separator = style.DockingSeparatorSize;
    const float squareWidth = std::floor(viewport->WorkSize.y * framebufferScale.y / framebufferScale.x);
    const auto fitPanelWidths = [&](float hierarchyWidth, float editorWidth)
    {
        const float availableWidth = std::max(viewport->WorkSize.x - 2.0f * separator, 1.0f);
        const float minimumWidth = std::min(style.WindowMinSize.x, availableWidth / 3.0f);
        const float sceneWidth = std::min(squareWidth, availableWidth - 2.0f * minimumWidth);
        const float panelWidth = availableWidth - sceneWidth;
        const float adjustment = (panelWidth - hierarchyWidth - editorWidth) * 0.5f;
        hierarchyWidth = std::floor(std::clamp(hierarchyWidth + adjustment, minimumWidth, panelWidth - minimumWidth));
        return std::pair{hierarchyWidth, panelWidth - hierarchyWidth};
    };

    const bool rebuildLayout = editorDockId == 0 || showObjectList != (hierarchyDockId != 0);
    // Keep the scene square when both side panels share the remaining width.
    if (!rebuildLayout && showObjectList)
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

    if (rebuildLayout)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(
            dockspaceId,
            static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) | ImGuiDockNodeFlags_PassthruCentralNode
        );
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        const float availableWidth = std::max(viewport->WorkSize.x - separator, 1.0f);
        const float minimumWidth = std::min(style.WindowMinSize.x, availableWidth * 0.5f);
        const float previousWidth = centerWidth + editorWidth;
        const float preferredEditorWidth = std::max(availableWidth - squareWidth, availableWidth * kEditorWidthRatio);
        float rightWidth = previousWidth > 0.0f ? availableWidth * editorWidth / previousWidth : preferredEditorWidth;
        rightWidth = std::clamp(rightWidth, minimumWidth, availableWidth - minimumWidth);
        float leftWidth = 0.0f;
        if (showObjectList)
        {
            const float preferredHierarchyWidth = hierarchyWidth > 0.0f ? hierarchyWidth :
                (availableWidth - rightWidth) * kHierarchyWidthRatio;
            const auto widths = fitPanelWidths(
                preferredHierarchyWidth, previousWidth > 0.0f ? editorWidth : rightWidth
            );
            leftWidth  = widths.first;
            rightWidth = widths.second;
        }
        ImGuiID centerDockId = dockspaceId;
        editorDockId = ImGui::DockBuilderSplitNode(
            centerDockId, ImGuiDir_Right, rightWidth / availableWidth, nullptr, &centerDockId
        );
        ImGui::DockBuilderSetNodeSize(editorDockId, {rightWidth, viewport->WorkSize.y});
        ImGui::DockBuilderDockWindow("Editor", editorDockId);
        const float remainingWidth = availableWidth - rightWidth;
        ImGui::DockBuilderSetNodeSize(centerDockId, {remainingWidth, viewport->WorkSize.y});
        hierarchyDockId = 0;
        if (showObjectList)
        {
            hierarchyDockId = ImGui::DockBuilderSplitNode(
                centerDockId, ImGuiDir_Left, leftWidth / std::max(remainingWidth - separator, 1.0f), nullptr, nullptr
            );
            ImGui::DockBuilderSetNodeSize(hierarchyDockId, {leftWidth, viewport->WorkSize.y});
            ImGui::DockBuilderDockWindow("Scene Objects", hierarchyDockId);
        }
        hierarchy.reset();
        ImGui::DockBuilderFinish(dockspaceId);
    }

    if (const ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId))
    {
        centerWidth = centralNode->Size.x;
        editorWidth = ImGui::DockBuilderGetNode(editorDockId)->Size.x;
        if (showObjectList)
            hierarchyWidth = ImGui::DockBuilderGetNode(hierarchyDockId)->Size.x;

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

    glm::mat4                     model        = transform.trsMatrix();
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
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    ImGui::Begin("Editor", nullptr, editorFlags);

    InspectorResult result;
    result.sceneId = drawSceneSelector(scene.id, dirty);
    if (result.sceneId)
        hierarchy.reset();

    auto& config   = *context().config;
    auto& renderer = *context().renderer;
    drawPerformance(context().profiler);

    result.edited |= drawCameraInspector(scene.editorCamera, renderer);
    drawRenderGraphSelector(config, renderer.vulkanContext().supportsRayTracing());
    result.edited |= drawEnvironmentInspector(scene, *context().assetManager);

    if (const auto* graph = renderer.getActiveGraph(); graph && graph->getName() == config.renderGraphName())
    {
        if (renderer.supportsObjectPicking())
        {
            bool objectPicking = config.editorConfig().objectPicking;
            if (ImGui::Checkbox("Object picking", &objectPicking))
                config.setObjectPicking(objectPicking);
        }
        renderer.renderUI({renderer.vulkanContext(), scene.editorCamera});
    }
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
    editorDockId    = 0;
    hierarchyDockId = 0;
    hierarchy.reset();
}
