#include "editor/editor_ui.h"

#include "scene/light.h"
#include "asset/asset_desc.h"
#include "scene/transform.h"

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

#include "logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
    void drawTransformEditor(Transform &transform)
    {
        ImGui::TextUnformatted("Transform");
        ImGui::Separator();
        ImGui::DragFloat3("Position", glm::value_ptr(transform.position), 0.05f);
        glm::vec3 rotationEulerDegrees = transform.rotationEulerDegrees();
        if (ImGui::DragFloat3(
                "Rotation",
                glm::value_ptr(rotationEulerDegrees),
                0.25f,
                0.0f,
                0.0f,
                "%.1f deg"))
        {
            transform.setRotationEulerDegrees(rotationEulerDegrees);
        }
        if (ImGui::DragFloat3("Scale", glm::value_ptr(transform.scale), 0.01f))
        {
            transform.scale = glm::max(transform.scale, glm::vec3{0.001f});
        }
    }

    void applyModelMatrix(Transform &transform, const glm::mat4 &model)
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

    glm::quat rotationFromDirection(const glm::vec3 &direction)
    {
        const glm::vec3 forward = glm::normalize(direction);
        const glm::vec3 up = std::abs(glm::dot(forward, glm::vec3{0.0f, 1.0f, 0.0f})) >
                0.999f
            ? glm::vec3{0.0f, 0.0f, 1.0f}
            : glm::vec3{0.0f, 1.0f, 0.0f};
        return glm::normalize(glm::quatLookAtRH(forward, up));
    }

    void drawDirectionEditor(Light &light)
    {
        glm::vec3 direction = light.direction;
        if (ImGui::DragFloat3("Direction", glm::value_ptr(direction), 0.01f) &&
            glm::dot(direction, direction) > 0.000001f)
        {
            light.direction = glm::normalize(direction);
        }
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

void EditorUI::drawGizmo(
    Transform &transform,
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
    }
}

void EditorUI::drawLightGizmo(
    Light &light,
    const glm::mat4 &view,
    const glm::mat4 &projection,
    bool enableShortcuts)
{
    const bool supportsPosition = light.type != Light::Type::Directional;
    const bool supportsRotation = light.type != Light::Type::Point;
    const ImGuiIO &io = ImGui::GetIO();
    if (enableShortcuts && !io.WantTextInput && !ImGui::IsAnyItemActive())
    {
        if (supportsPosition && ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            gizmoOperation = 0;
        }
        else if (supportsRotation && ImGui::IsKeyPressed(ImGuiKey_E, false))
        {
            gizmoOperation = 1;
        }
    }

    if ((!supportsPosition && gizmoOperation == 0) ||
        (!supportsRotation && gizmoOperation == 1) || gizmoOperation == 2)
    {
        gizmoOperation = supportsPosition ? 0 : 1;
    }

    Transform transform;
    transform.position = light.position;
    if (supportsRotation)
    {
        transform.rotation = rotationFromDirection(light.direction);
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(scenePosition.x, scenePosition.y, sceneSize.x, sceneSize.y);

    glm::mat4 model = transform.matrix();
    const ImGuizmo::OPERATION operation = gizmoOperation == 0
        ? ImGuizmo::TRANSLATE
        : ImGuizmo::ROTATE;
    ImGuizmo::Manipulate(
        glm::value_ptr(view),
        glm::value_ptr(projection),
        operation,
        ImGuizmo::LOCAL,
        glm::value_ptr(model));

    if (ImGuizmo::IsUsing())
    {
        applyModelMatrix(transform, model);
        if (supportsPosition)
        {
            light.position = transform.position;
        }
        if (supportsRotation)
        {
            light.direction = glm::normalize(
                transform.rotation * glm::vec3{0.0f, 0.0f, -1.0f});
        }
    }
}

void EditorUI::drawInspector(SceneObjectDesc *object, Light *light)
{
    ImGui::SetNextWindowDockID(editorDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags editorFlags = ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoCollapse |
                                              ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Editor", nullptr, editorFlags);

    ImGui::TextUnformatted("Performance");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", displayedFps);

    ImGui::Spacing();
    if (object != nullptr)
    {
        ImGui::Text("Selected: %s", object->name.c_str());
        drawTransformEditor(object->transform);
    }
    else if (light != nullptr)
    {
        ImGui::Text("Selected: %s", light->name.c_str());
        ImGui::TextUnformatted("Light");
        ImGui::Separator();

        static constexpr const char *typeNames[] = {
            "Point",
            "Directional",
            "Rect Area",
            "Spot",
        };
        int type = static_cast<int>(light->type);
        if (ImGui::Combo("Type", &type, typeNames, IM_ARRAYSIZE(typeNames)))
        {
            light->type = static_cast<Light::Type>(type);
        }

        if (light->type != Light::Type::Directional)
        {
            ImGui::DragFloat3("Position", glm::value_ptr(light->position), 0.05f);
        }
        if (light->type != Light::Type::Point)
        {
            drawDirectionEditor(*light);
        }

        ImGui::ColorEdit3("Color", glm::value_ptr(light->color));
        ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 100000.0f);
        light->intensity = std::max(light->intensity, 0.0f);

        if (light->type == Light::Type::Point || light->type == Light::Type::Spot)
        {
            ImGui::DragFloat("Range", &light->range, 0.1f, 0.01f, 100000.0f);
            light->range = std::max(light->range, 0.01f);
        }
        if (light->type == Light::Type::Spot)
        {
            float innerAngle = glm::degrees(std::acos(std::clamp(
                light->cosInner, -1.0f, 1.0f)));
            float outerAngle = glm::degrees(std::acos(std::clamp(
                light->cosOuter, -1.0f, 1.0f)));
            if (ImGui::DragFloat("Inner Angle", &innerAngle, 0.25f, 0.0f, 89.0f, "%.1f deg"))
            {
                innerAngle = std::clamp(innerAngle, 0.0f, outerAngle);
                light->cosInner = std::cos(glm::radians(innerAngle));
            }
            if (ImGui::DragFloat("Outer Angle", &outerAngle, 0.25f, 0.0f, 89.0f, "%.1f deg"))
            {
                outerAngle = std::clamp(outerAngle, innerAngle, 89.0f);
                light->cosOuter = std::cos(glm::radians(outerAngle));
            }
        }
        if (light->type == Light::Type::RectArea)
        {
            if (ImGui::DragFloat2(
                    "Area Size",
                    glm::value_ptr(light->areaSize),
                    0.05f,
                    0.01f,
                    100000.0f))
            {
                light->areaSize = glm::max(light->areaSize, glm::vec2{0.01f});
            }
        }
        ImGui::Checkbox("Enabled", &light->enabled);
        ImGui::Checkbox("Cast Shadow", &light->castShadow);
    }
    else
    {
        ImGui::TextUnformatted("No selection");
    }

    if (object != nullptr || light != nullptr)
    {
        ImGui::Spacing();
        ImGui::TextUnformatted("Gizmo");
        if (object != nullptr || light->type != Light::Type::Directional)
        {
            ImGui::RadioButton("Move (W)", &gizmoOperation, 0);
        }
        if (object != nullptr || light->type != Light::Type::Point)
        {
            if (object != nullptr || light->type != Light::Type::Directional)
            {
                ImGui::SameLine();
            }
            ImGui::RadioButton("Rotate (E)", &gizmoOperation, 1);
        }
        if (object != nullptr)
        {
            ImGui::SameLine();
            ImGui::RadioButton("Scale (R)", &gizmoOperation, 2);
        }
    }

    ImGui::End();
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
