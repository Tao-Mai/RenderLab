#include "editor/EditorUi.h"

#include <imgui.h>

// ImGuizmo requires ImGui types to be declared before its header.
#include <ImGuizmo.h>
#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
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
void EditorUI::beginFrame(float deltaTime)
{
    ImGuizmo::BeginFrame();

    fpsRefreshTime += deltaTime;
    ++framesSinceRefresh;
    if (fpsRefreshTime >= 0.5f)
    {
        displayedFps       = static_cast<float>(framesSinceRefresh) / fpsRefreshTime;
        fpsRefreshTime     = 0.0f;
        framesSinceRefresh = 0;
    }

    const ImGuiViewport*     viewport = ImGui::GetMainViewport();
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

        ImGuiID centerDockId = dockspaceId;
        editorDockId         = ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Right, 0.24f, nullptr, &centerDockId);
        ImGui::DockBuilderDockWindow("Editor", editorDockId);
        hierarchyDockId = ImGui::DockBuilderSplitNode(centerDockId, ImGuiDir_Left, 0.23f, nullptr, &centerDockId);
        ImGui::DockBuilderDockWindow("Scene Objects", hierarchyDockId);
        ImGui::DockBuilderFinish(dockspaceId);
        dockLayoutInitialized = true;
    }

    if (const ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId))
    {
        scenePosition = {centralNode->Pos.x, centralNode->Pos.y};
        sceneSize     = {
            std::max(centralNode->Size.x, 1.0f),
            std::max(centralNode->Size.y, 1.0f),
        };
        const ImGuiIO& io = ImGui::GetIO();
        scenePixels       = {
                  .x      = (centralNode->Pos.x - viewport->Pos.x) * io.DisplayFramebufferScale.x,
                  .y      = (centralNode->Pos.y - viewport->Pos.y) * io.DisplayFramebufferScale.y,
                  .width  = centralNode->Size.x * io.DisplayFramebufferScale.x,
                  .height = centralNode->Size.y * io.DisplayFramebufferScale.y,
        };
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
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(scenePosition.x, scenePosition.y, sceneSize.x, sceneSize.y);

    glm::mat4                     model        = transform.matrix();
    constexpr ImGuizmo::OPERATION operations[] = {
        ImGuizmo::TRANSLATE,
        ImGuizmo::ROTATE,
        ImGuizmo::SCALE,
    };
    const ImGuizmo::OPERATION operation = operations[std::clamp(gizmoOperation, 0, 2)];
    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        operation,
        ImGuizmo::LOCAL,
        glm::value_ptr(model)
    );

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

    ImGui::TextUnformatted("Performance");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", displayedFps);
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

    drawRendererSettings(
        *context().config,
        *context().assetManager,
        context().renderer->vulkanContext(),
        scene.editorCamera.nearPlane
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
    if (!inside || ImGui::GetIO().WantCaptureMouse || !context().inputManager->get(Command::SelectObject) ||
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

float EditorUI::sceneAspectRatio() const { return sceneSize.x / std::max(sceneSize.y, 1.0f); }


void EditorUI::resetLayout()
{
    dockLayoutInitialized = false;
    editorDockId          = 0;
    hierarchyDockId       = 0;
    hierarchy.reset();
}
