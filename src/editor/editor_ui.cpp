#include "editor/editor_ui.h"

#include "asset/asset_desc.h"
#include "core/logger.h"
#include "ecs/transform.h"
#include "editor/component_draw.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
    void applyModelMatrix(ecs::Transform& transform, const glm::mat4& model)
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
        transform.scale = scale;
    }
}

EditorUI::~EditorUI()
{
    shutdown();
}

void EditorUI::init(
    GLFWwindow *window,
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamily,
    VkQueue queue,
    VkFormat targetColorFormat,
    uint32_t minImageCount,
    uint32_t imageCount)
{
    if (contextCreated)
    {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated = true;

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

#ifdef _WIN32
    std::array<char, MAX_PATH> windowsDirectory{};
    const UINT directoryLength = GetWindowsDirectoryA(
        windowsDirectory.data(),
        static_cast<UINT>(windowsDirectory.size()));
    CHECK(directoryLength != 0 && directoryLength < windowsDirectory.size(),
        "failed to locate the Windows font directory");
    const std::string fontPath =
        std::string{windowsDirectory.data(), directoryLength} + "\\Fonts\\msyh.ttc";
    CHECK(io.Fonts->AddFontFromFileTTF(
            fontPath.c_str(),
            18.0f,
            nullptr,
            io.Fonts->GetGlyphRangesChineseFull()) != nullptr,
        "failed to load Microsoft YaHei: {}", fontPath);
#else
#error RenderLab editor requires Windows to load Microsoft YaHei
#endif

    CHECK(ImGui_ImplGlfw_InitForVulkan(window, true),
        "failed to init ImGui GLFW backend");
    glfwBackendInitialized = true;

    colorFormat = targetColorFormat;
    pipelineRenderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
    };

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_4;
    initInfo.Instance = instance;
    initInfo.PhysicalDevice = physicalDevice;
    initInfo.Device = device;
    initInfo.QueueFamily = queueFamily;
    initInfo.Queue = queue;
    initInfo.DescriptorPoolSize = 64;
    initInfo.MinImageCount = minImageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineRenderingCreateInfo = pipelineRenderingInfo;

    CHECK(ImGui_ImplVulkan_Init(&initInfo),
        "failed to init ImGui Vulkan backend");
    vulkanBackendInitialized = true;
}

void EditorUI::beginFrame(float deltaTime)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    fpsRefreshTime += deltaTime;
    ++framesSinceRefresh;
    if (fpsRefreshTime >= 0.5f)
    {
        displayedFps = static_cast<float>(framesSinceRefresh) / fpsRefreshTime;
        fpsRefreshTime = 0.0f;
        framesSinceRefresh = 0;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode |
                                         ImGuiDockNodeFlags_NoDockingOverCentralNode;
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, viewport, dockFlags);

    if (!dockLayoutInitialized)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(
            dockspaceId,
            ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID centerDockId = dockspaceId;
        editorDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            0.24f,
            nullptr,
            &centerDockId);
        ImGui::DockBuilderDockWindow("Editor", editorDockId);
        ImGui::DockBuilderFinish(dockspaceId);
        dockLayoutInitialized = true;
    }

    if (const ImGuiDockNode *centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId))
    {
        scenePosition = {centralNode->Pos.x, centralNode->Pos.y};
        sceneSize = {
            std::max(centralNode->Size.x, 1.0f),
            std::max(centralNode->Size.y, 1.0f),
        };
        const ImGuiIO &io = ImGui::GetIO();
        scenePixels = {
            .x = (centralNode->Pos.x - viewport->Pos.x) * io.DisplayFramebufferScale.x,
            .y = (centralNode->Pos.y - viewport->Pos.y) * io.DisplayFramebufferScale.y,
            .width = centralNode->Size.x * io.DisplayFramebufferScale.x,
            .height = centralNode->Size.y * io.DisplayFramebufferScale.y,
        };
    }
}

bool EditorUI::drawGizmo(
    ecs::Transform&transform,
    const glm::mat4 &view,
    const glm::mat4 &projection,
    bool enableShortcuts)
{
    const ImGuiIO &io = ImGui::GetIO();
    if (enableShortcuts && !io.WantTextInput && !ImGui::IsAnyItemActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            gizmoOperation = 0;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        {
            gizmoOperation = 1;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            gizmoOperation = 2;
        }
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(scenePosition.x, scenePosition.y, sceneSize.x, sceneSize.y);

    glm::mat4 model = transform.matrix();
    constexpr ImGuizmo::OPERATION operations[] = {
        ImGuizmo::TRANSLATE,
        ImGuizmo::ROTATE,
        ImGuizmo::SCALE,
    };
    const ImGuizmo::OPERATION operation = operations[
        std::clamp(gizmoOperation, 0, 2)];
    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        operation,
        ImGuizmo::LOCAL,
        glm::value_ptr(model));

    if (ImGuizmo::IsUsing())
    {
        applyModelMatrix(transform, model);
        return true;
    }
    return false;
}

bool EditorUI::drawInspector(SceneObjectDesc *object, std::string_view sceneName, bool dirty)
{
    ImGui::SetNextWindowDockID(editorDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags editorFlags = ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoCollapse |
                                              ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Editor", nullptr, editorFlags);

    ImGui::TextUnformatted("Performance");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", displayedFps);
    if (dirty)
    {
        ImGui::Text("%.*s*", static_cast<int>(sceneName.size()), sceneName.data());
    }
    else
    {
        ImGui::TextUnformatted(sceneName.data(), sceneName.data() + sceneName.size());
    }

    bool edited = false;
    ImGui::Spacing();
    if (object != nullptr)
    {
        ImGui::Text("Selected: %s", object->name.c_str());
        for (auto& [typeName, component] : object->components)
        {
            ImGui::Spacing();
            ImGui::PushID(typeName.c_str());
            edited = drawComponent(typeName, component) || edited;
            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextUnformatted("No selection");
    }

    ImGui::End();
    return edited;
}

void EditorUI::endFrame()
{
    ImGui::Render();
}

void EditorUI::render(VkCommandBuffer commandBuffer) const
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

bool EditorUI::sceneClicked(glm::vec2 &mousePosition) const
{
    const ImVec2 mouse = ImGui::GetMousePos();
    const bool inside = mouse.x >= scenePosition.x && mouse.y >= scenePosition.y &&
                        mouse.x < scenePosition.x + sceneSize.x &&
                        mouse.y < scenePosition.y + sceneSize.y;
    if (!inside || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGuizmo::IsOver() || ImGuizmo::IsUsing())
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
    if (!contextCreated)
    {
        return false;
    }
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

bool EditorUI::saveRequested() const
{
    if (!contextCreated)
    {
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput)
    {
        return false;
    }
    return ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S);
}

float EditorUI::sceneAspectRatio() const
{
    return sceneSize.x / std::max(sceneSize.y, 1.0f);
}

ViewportRect EditorUI::sceneViewportPixels() const
{
    return scenePixels;
}

void EditorUI::shutdown() noexcept
{
    if (vulkanBackendInitialized)
    {
        ImGui_ImplVulkan_Shutdown();
        vulkanBackendInitialized = false;
    }
    if (glfwBackendInitialized)
    {
        ImGui_ImplGlfw_Shutdown();
        glfwBackendInitialized = false;
    }
    if (contextCreated)
    {
        ImGui::DestroyContext();
        contextCreated = false;
    }
    dockLayoutInitialized = false;
    editorDockId = 0;
}
