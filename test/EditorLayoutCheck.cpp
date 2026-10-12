#include "core/Logger.h"
#include "editor/EditorUi.h"

#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

// A small layout-only check: no window, GPU, assets, or saved ImGui settings.
int main(int, char** argv)
{
    logger::init(argv[0]);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    EditorUI ui;
    struct Layout
    {
        ImVec2 size;
        ImVec2 scale;
    };
    for (const auto layout : {
             Layout{{2000, 1000}, {1, 1}}, Layout{{1600, 900}, {1, 1}},
             Layout{{2400, 1200}, {1, 1}}, Layout{{1000, 500}, {2, 2}},
             Layout{{900, 1000}, {1, 1}}, Layout{{400, 1000}, {1, 1}},
             Layout{{1500, 900}, {2, 1}}
         })
    {
        io.DisplaySize = layout.size;
        io.DisplayFramebufferScale = layout.scale;
        for (int frame = 0; frame < 4; ++frame)
        {
            ImGui::NewFrame();
            ui.beginFrame(io.DeltaTime);
            ImGui::Begin("Scene Objects");
            ImGui::End();
            ImGui::Begin("Editor");
            ImGui::End();
            ui.endFrame();
        }

        const auto viewport = ui.sceneViewportPixels();
        const auto* left = ImGui::FindWindowByName("Scene Objects");
        const auto* right = ImGui::FindWindowByName("Editor");
        const auto* root = left->DockNode;
        while (root->ParentNode)
            root = root->ParentNode;
        const auto* center = root->CentralNode;
        CHECK(center);
        CHECK(viewport.width == viewport.height);
        CHECK(center->Size.x * layout.scale.x >= viewport.width);
        CHECK(center->Size.y * layout.scale.y >= viewport.height);
        CHECK(viewport.x >= center->Pos.x * layout.scale.x);
        CHECK(viewport.y >= center->Pos.y * layout.scale.y);
        CHECK(viewport.x + viewport.width <= (center->Pos.x + center->Size.x) * layout.scale.x);
        CHECK(viewport.y + viewport.height <= (center->Pos.y + center->Size.y) * layout.scale.y);
        const float separator = ImGui::GetStyle().DockingSeparatorSize;
        CHECK(std::abs(left->Pos.x + left->Size.x + separator - center->Pos.x) < 1.0f);
        CHECK(std::abs(center->Pos.x + center->Size.x + separator - right->Pos.x) < 1.0f);
        LOG_INFO("Square dock layout passed: {} x {}, framebuffer scale {} x {}",
                 layout.size.x, layout.size.y, layout.scale.x, layout.scale.y);
    }

    const auto viewport = ui.sceneViewportPixels();
    const auto* mainViewport = ImGui::GetMainViewport();
    const ImVec2 sceneCenter{
        mainViewport->Pos.x + (viewport.x + viewport.width * 0.5f) / io.DisplayFramebufferScale.x,
        mainViewport->Pos.y + (viewport.y + viewport.height * 0.5f) / io.DisplayFramebufferScale.y,
    };
    const auto* panel = ImGui::FindWindowByName("Scene Objects");
    const ImVec2 panelCenter{panel->Pos.x + panel->Size.x * 0.5f, panel->Pos.y + panel->Size.y * 0.5f};
    const auto frameAt = [&](ImVec2 position, int button, bool pressed)
    {
        io.AddMousePosEvent(position.x, position.y);
        io.AddMouseButtonEvent(button, pressed);
        ImGui::NewFrame();
        ui.beginFrame(io.DeltaTime);
        ImGui::Begin("Scene Objects");
        ImGui::End();
        ImGui::Begin("Editor");
        ImGui::End();
        ui.endFrame();
    };

    frameAt(sceneCenter, ImGuiMouseButton_Left, false);
    frameAt(sceneCenter, ImGuiMouseButton_Left, true);
    CHECK(ui.isSceneFocused());
    frameAt(sceneCenter, ImGuiMouseButton_Left, false);
    frameAt(panelCenter, ImGuiMouseButton_Left, false);
    CHECK(ui.isSceneFocused());
    frameAt(panelCenter, ImGuiMouseButton_Left, true);
    CHECK(!ui.isSceneFocused());
    frameAt(panelCenter, ImGuiMouseButton_Left, false);
    frameAt(sceneCenter, ImGuiMouseButton_Left, false);
    CHECK(!ui.isSceneFocused());
    frameAt(sceneCenter, ImGuiMouseButton_Right, true);
    CHECK(ui.isSceneFocused());
    frameAt(sceneCenter, ImGuiMouseButton_Right, false);
    LOG_INFO("Viewport focus follows clicks, not mouse movement");

    // ImGuizmo captures the next frame's mouse while hovered/dragged, without hovering a UI window.
    ImGui::SetNextFrameWantCaptureMouse(true);
    frameAt(sceneCenter, ImGuiMouseButton_Left, true);
    CHECK(io.WantCaptureMouse);
    CHECK(ui.isSceneFocused());
    frameAt(sceneCenter, ImGuiMouseButton_Left, false);
    ImGui::SetNextFrameWantCaptureMouse(true);
    frameAt(panelCenter, ImGuiMouseButton_Left, true);
    CHECK(!ui.isSceneFocused());
    frameAt(panelCenter, ImGuiMouseButton_Left, false);
    LOG_INFO("Gizmo mouse capture preserves viewport focus; panel clicks still clear it");

    ui.shutdown();
    ImGui::DestroyContext();
}
