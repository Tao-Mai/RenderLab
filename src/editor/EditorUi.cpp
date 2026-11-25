#include "editor/EditorUi.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "editor/PassEditor.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace
{
constexpr float kEditorWidthRatio     = 0.24f;
constexpr float kHierarchyWidthRatio  = 0.23f;

constexpr double kFpsUpdateIntervalSeconds = 0.5;

std::optional<Scene::ID> drawSceneSelector(const Scene::ID& current)
{
    std::optional<Scene::ID> chosen;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##Scene", current.c_str()))
    {

        auto sceneIds = context().assetManager->loadedIds<Scene>();
        std::ranges::sort(sceneIds, {}, &Scene::ID::value);
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

    return chosen;
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
    const ImGuiIO& io = ImGui::GetIO();

    fpsElapsedSeconds += io.DeltaTime;
    ++fpsFrameCount;
    if (fpsElapsedSeconds >= kFpsUpdateIntervalSeconds)
    {
        displayedFps = static_cast<float>(fpsFrameCount / fpsElapsedSeconds);
        fpsElapsedSeconds = 0.0;
        fpsFrameCount = 0;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const glm::vec2      framebufferScale{io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y};

    const ImGuiStyle& style          = ImGui::GetStyle();
    const float       separator      = style.DockingSeparatorSize;
    const float       squareWidth    = std::floor(viewport->WorkSize.y * framebufferScale.y / framebufferScale.x);
    const auto        fitPanelWidths = [&](float hierarchyWidth, float editorWidth)
    {
        const float availableWidth = std::max(viewport->WorkSize.x - 2.0f * separator, 1.0f);
        const float minimumWidth   = std::min(style.WindowMinSize.x, availableWidth / 3.0f);
        const float sceneWidth     = std::min(squareWidth, availableWidth - 2.0f * minimumWidth);
        const float panelWidth     = availableWidth - sceneWidth;
        const float adjustment     = (panelWidth - hierarchyWidth - editorWidth) * 0.5f;
        hierarchyWidth = std::floor(std::clamp(hierarchyWidth + adjustment, minimumWidth, panelWidth - minimumWidth));
        return std::pair{hierarchyWidth, panelWidth - hierarchyWidth};
    };

    const bool rebuildLayout = editorDockId == 0 || showObjectList != (hierarchyDockId != 0);
    // Keep the scene square when both side panels share the remaining width.
    if (!rebuildLayout && showObjectList)
    {
        const auto* hierarchyNode = ImGui::DockBuilderGetNode(hierarchyDockId);
        const auto* editorNode    = ImGui::DockBuilderGetNode(editorDockId);

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

        const float availableWidth       = std::max(viewport->WorkSize.x - separator, 1.0f);
        const float minimumWidth         = std::min(style.WindowMinSize.x, availableWidth * 0.5f);
        const float previousWidth        = centerWidth + editorWidth;
        const float preferredEditorWidth = std::max(availableWidth - squareWidth, availableWidth * kEditorWidthRatio);
        float rightWidth = previousWidth > 0.0f ? availableWidth * editorWidth / previousWidth : preferredEditorWidth;
        rightWidth       = std::clamp(rightWidth, minimumWidth, availableWidth - minimumWidth);
        float leftWidth  = 0.0f;
        if (showObjectList)
        {
            const float preferredHierarchyWidth =
                hierarchyWidth > 0.0f ? hierarchyWidth : (availableWidth - rightWidth) * kHierarchyWidthRatio;
            const auto widths =
                fitPanelWidths(preferredHierarchyWidth, previousWidth > 0.0f ? editorWidth : rightWidth);
            leftWidth  = widths.first;
            rightWidth = widths.second;
        }
        ImGuiID centerDockId = dockspaceId;
        editorDockId         = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            rightWidth / availableWidth,
            nullptr,
            &centerDockId
        );
        ImGui::DockBuilderSetNodeSize(editorDockId, {rightWidth, viewport->WorkSize.y});
        ImGui::DockBuilderDockWindow("Editor", editorDockId);
        const float remainingWidth = availableWidth - rightWidth;
        ImGui::DockBuilderSetNodeSize(centerDockId, {remainingWidth, viewport->WorkSize.y});
        hierarchyDockId = 0;
        if (showObjectList)
        {
            hierarchyDockId = ImGui::DockBuilderSplitNode(
                centerDockId,
                ImGuiDir_Left,
                leftWidth / std::max(remainingWidth - separator, 1.0f),
                nullptr,
                nullptr
            );
            ImGui::DockBuilderSetNodeSize(hierarchyDockId, {leftWidth, viewport->WorkSize.y});
            ImGui::DockBuilderDockWindow("Scene Objects", hierarchyDockId);
        }
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

        // Fit and center the scene texture in a pixel-aligned square.
        const float side = std::max(1.0f, std::floor(std::min(availablePixels.x, availablePixels.y)));
        const glm::vec2 offset{
            centralNode->Pos.x - viewport->Pos.x,
            centralNode->Pos.y - viewport->Pos.y,
        };
        const auto origin = glm::floor(offset * framebufferScale + (availablePixels - side) * 0.5f);
        scenePixels = {origin.x, origin.y, side, side};
    }
}

void EditorUI::drawSceneList(const Scene& scene) const
{
    if (!hierarchyDockId)
        return;

    ImGui::SetNextWindowDockID(hierarchyDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags kWindowFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Scene Objects", nullptr, kWindowFlags))
    {
        ImGui::SeparatorText("Objects");
        for (const auto& object : scene.objects)
            ImGui::TextUnformatted(object.name.c_str());

        ImGui::SeparatorText("Lights");
        for (const auto& light : scene.analyticLights)
            ImGui::TextUnformatted(light.name.c_str());
    }
    ImGui::End();
}

std::optional<Scene::ID> EditorUI::drawControls(const Scene& scene)
{
    ImGui::SetNextWindowDockID(editorDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags kEditorFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Editor", nullptr, kEditorFlags);
    ImGui::Text("FPS: %.1f", displayedFps);

    auto sceneId = drawSceneSelector(scene.id);
    auto& settings = context().config.editor;
    ImGui::Checkbox("Scene objects", &settings.showSceneObjects);

    auto& renderer = *context().renderer;
    renderer.renderUI({renderer.getDevice(), scene.camera});

    ImGui::End();

    return sceneId;
}

void EditorUI::endFrame() { ImGui::Render(); }

bool EditorUI::wantsInput() const
{
    if (!ImGui::GetCurrentContext())
    {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void EditorUI::resetLayout()
{
    editorDockId    = 0;
    hierarchyDockId = 0;
}
