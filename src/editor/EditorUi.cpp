#include "editor/EditorUi.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "asset/import/ImportId.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Reflect.h"
#include "render/Renderer.h"
#include "scene/component/TransformComponent.h"
#include "editor/ComponentDraw.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <string>
#include <system_error>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#endif

namespace
{
void drawActorTypeTree(
    std::type_index parent,
    const std::vector<std::pair<std::type_index, std::string>>& types,
    std::optional<std::string>& chosen)
{
    const auto& registry = BaseTypeInfo<AActor>::baseTypeInfoMap;
    for (const auto& [type, name] : types)
    {
        const auto registered = registry.find(name);
        DCHECK(registered != registry.end());
        if (registered->second.parentType != parent) continue;

        const bool hasChildren = std::ranges::any_of(registry, [&](const auto& entry) {
            return entry.second.parentType == type;
        });
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
            ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        const bool open = ImGui::TreeNodeEx(name.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) chosen = name;
        if (open && hasChildren)
        {
            drawActorTypeTree(type, types, chosen);
            ImGui::TreePop();
        }
    }
}

[[nodiscard]] std::string displayPath(const std::filesystem::path& path)
{
    const std::u8string utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

void drawSourcePath(std::filesystem::path& source, const wchar_t* filter)
{
    if (ImGui::Button("Source file..."))
    {
        std::array<wchar_t, 32768> selectedFile{};
        const std::wstring         initialDirectory =
            context().config->paths().assets.parent_path().wstring();

        OPENFILENAMEW dialog{};
        dialog.lStructSize     = sizeof(dialog);
        dialog.hwndOwner       = GetActiveWindow();
        dialog.lpstrFilter     = filter;
        dialog.lpstrFile       = selectedFile.data();
        dialog.nMaxFile        = static_cast<DWORD>(selectedFile.size());
        dialog.lpstrInitialDir = initialDirectory.c_str();
        dialog.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
            OFN_EXPLORER | OFN_NOCHANGEDIR;

        if (GetOpenFileNameW(&dialog))
        {
            source = selectedFile.data();
        }
    }

    if (!source.empty())
    {
        const std::string label = displayPath(source);
        ImGui::TextWrapped("%s", label.c_str());
    }
}

[[nodiscard]] bool sourceIsImportable(
    const std::filesystem::path& source, int assetType)
{
    std::error_code error;
    if (source.empty() || !std::filesystem::is_regular_file(source, error))
    {
        return false;
    }

    std::string extension = source.extension().string();
    std::ranges::transform(extension,
                           extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (assetType == 2)
    {
        return extension == ".gltf" || extension == ".glb";
    }
    return extension == ".exr" || extension == ".png" ||
        extension == ".jpg" || extension == ".jpeg" || extension == ".hdr" ||
        extension == ".bmp" || extension == ".tga" ||
        extension == ".gif" || extension == ".psd" ||
        extension == ".pic" || extension == ".pnm";
}

void drawColorSpace(std::optional<ColorSpace>& colorSpace)
{
    int selected = 0;
    if (colorSpace)
    {
        selected = *colorSpace == ColorSpace::Linear ? 1 : 2;
    }
    if (ImGui::Combo("Color space", &selected, "Auto\0Linear\0sRGB\0"))
    {
        colorSpace = selected == 0
            ? std::nullopt
            : std::optional{selected == 1 ? ColorSpace::Linear : ColorSpace::Srgb};
    }
}

void drawPositiveUint(const char* label, uint32_t& value)
{
    constexpr uint32_t step = 1;
    ImGui::InputScalar(label, ImGuiDataType_U32, &value, &step);
}

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
}

EditorUI::~EditorUI()
{
    DCHECK(!contextCreated && !glfwBackendInitialized && !vulkanBackendInitialized);
    DCHECK(!environmentImport.valid());
}

void EditorUI::init(
    GLFWwindow*      window,
    VkInstance       instance,
    VkPhysicalDevice physicalDevice,
    VkDevice         device,
    uint32_t         queueFamily,
    VkQueue          queue,
    VkFormat         targetColorFormat,
    uint32_t         minImageCount,
    uint32_t         imageCount)
{
    if (contextCreated)
    {
        return;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated = true;

    ImGuiIO& io    = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

#ifdef _WIN32
    std::array<char, MAX_PATH> windowsDirectory{};
    const UINT                 directoryLength = GetWindowsDirectoryA(
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
          "failed to load Microsoft YaHei: {}",
          fontPath);
#else
#error RenderLab editor requires Windows to load Microsoft YaHei
#endif

    CHECK(ImGui_ImplGlfw_InitForVulkan(window, true),
          "failed to init ImGui GLFW backend");
    glfwBackendInitialized = true;

    colorFormat           = targetColorFormat;
    pipelineRenderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
    };

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion                  = VK_API_VERSION_1_4;
    initInfo.Instance                    = instance;
    initInfo.PhysicalDevice              = physicalDevice;
    initInfo.Device                      = device;
    initInfo.QueueFamily                 = queueFamily;
    initInfo.Queue                       = queue;
    initInfo.DescriptorPoolSize          = 64;
    initInfo.MinImageCount               = minImageCount;
    initInfo.ImageCount                  = imageCount;
    initInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering         = true;
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
        displayedFps       = static_cast<float>(framesSinceRefresh) / fpsRefreshTime;
        fpsRefreshTime     = 0.0f;
        framesSinceRefresh = 0;
    }

    const ImGuiViewport*     viewport  = ImGui::GetMainViewport();
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
        editorDockId         = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            0.24f,
            nullptr,
            &centerDockId);
        ImGui::DockBuilderDockWindow("Editor", editorDockId);
        hierarchyDockId = ImGui::DockBuilderSplitNode(
            centerDockId, ImGuiDir_Left, 0.23f, nullptr, &centerDockId);
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
            .x = (centralNode->Pos.x - viewport->Pos.x) * io.DisplayFramebufferScale.x,
            .y = (centralNode->Pos.y - viewport->Pos.y) * io.DisplayFramebufferScale.y,
            .width = centralNode->Size.x * io.DisplayFramebufferScale.x,
            .height = centralNode->Size.y * io.DisplayFramebufferScale.y,
        };
    }
}

EditorUI::HierarchyResult EditorUI::drawHierarchy(SceneAsset& scene, uint32_t& selectedId)
{
    HierarchyResult result;
    ImGui::SetNextWindowDockID(hierarchyDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Scene Objects", nullptr, flags);

    ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::BeginCombo("##Scene", scene.id.c_str()))
    {
        DCHECK(context().assetManager);
        auto sceneIds = context().assetManager->loadedIds<SceneAsset>();
        std::ranges::sort(sceneIds, {}, &SceneAsset::ID::value);
        for (const auto& id : sceneIds)
        {
            const bool selected = id == scene.id;
            if (ImGui::Selectable(id.c_str(), selected) && !selected)
            {
                result.sceneId = id;
                renameActorId = 0;
                focusRename = false;
                renameText.clear();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Switch scenes and discard unsaved scene changes");
    ImGui::SameLine();
    if (ImGui::Button("+")) ImGui::OpenPopup("New Actor");
    if (ImGui::BeginPopup("New Actor"))
    {
        ImGui::TextUnformatted("Choose an Actor class");
        ImGui::Separator();
        std::vector<std::pair<std::type_index, std::string>> types{
            BaseTypeInfo<AActor>::typeNames.begin(), BaseTypeInfo<AActor>::typeNames.end()};
        std::ranges::sort(types, {}, &std::pair<std::type_index, std::string>::second);
        if (ImGui::TreeNodeEx("AActor", ImGuiTreeNodeFlags_DefaultOpen |
            ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            drawActorTypeTree(typeid(AActor), types, result.createType);
            ImGui::TreePop();
        }
        if (result.createType) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();
    if (renameActorId &&
        !std::ranges::any_of(scene.actors, [&](const auto& actor) { return actor->selectionId() == renameActorId; }))
    {
        renameActorId = 0;
        focusRename   = false;
        renameText.clear();
    }

    if (ImGui::BeginChild("Actor list", {0, 0}))
    {
        for (const auto& actor : scene.actors)
        {
            ImGui::PushID(static_cast<int>(actor->selectionId()));
            if (actor->selectionId() == renameActorId)
            {
                if (focusRename)
                {
                    ImGui::SetKeyboardFocusHere();
                    focusRename = false;
                }
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x, 0.0f});
                const bool enter = ImGui::InputText(
                    "##Actor name",
                    &renameText,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
                );
                const bool finished = enter || ImGui::IsItemDeactivated();
                ImGui::PopStyleVar();

                if (finished)
                {
                    if (!renameText.empty() && actor->name != renameText)
                    {
                        actor->name    = renameText;
                        result.renamed = true;
                    }
                    renameActorId = 0;
                    renameText.clear();
                }
            }
            else
            {
                const std::string label = actor->name + "###Actor";
                if (ImGui::Selectable(label.c_str(), actor->selectionId() == selectedId))
                    selectedId = actor->selectionId();
            }

            if (ImGui::BeginPopupContextItem("Actor actions"))
            {
                selectedId = actor->selectionId();
                if (ImGui::MenuItem("Rename"))
                {
                    renameActorId = selectedId;
                    renameText    = actor->name;
                    focusRename   = true;
                }
                if (ImGui::MenuItem("Delete"))
                    result.deleteId = actor->selectionId();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return result;
}

bool EditorUI::drawGizmo(
    TransformComponent& transform,
    const glm::mat4&         view,
    const glm::mat4&         projection,
    bool                     enableShortcuts)
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

EditorUI::InspectorResult EditorUI::drawInspector(
    SceneAsset& scene, AActor* object, bool dirty)
{
    ImGui::SetNextWindowDockID(editorDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags editorFlags = ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Editor", nullptr, editorFlags);
    finishEnvironmentImport();

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
    if (ImGui::CollapsingHeader("Editor camera"))
    {
        auto& camera = scene.editorCamera;
        result.edited |= ImGui::DragFloat3("Camera position", glm::value_ptr(camera.position), 0.05f);
        result.edited |= ImGui::DragFloat("Camera yaw", &camera.yaw, 0.2f, 0.0f, 0.0f, "%.1f deg");
        result.edited |= ImGui::DragFloat("Camera pitch", &camera.pitch, 0.2f, -89.0f, 89.0f,
                                         "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        result.edited |= ImGui::DragFloat("Camera FOV", &camera.fieldOfView, 0.2f, 20.0f, 90.0f,
                                         "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        result.edited |= ImGui::DragFloat("Camera near", &camera.nearPlane, 0.01f, 0.001f,
                                         camera.farPlane - 0.001f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        result.edited |= ImGui::DragFloat("Camera far", &camera.farPlane, 0.5f,
                                         camera.nearPlane + 0.001f, 100000.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        result.edited |= ImGui::DragFloat("Camera sensitivity", &camera.mouseSensitivity, 0.001f,
                                         0.001f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        result.edited |= ImGui::DragFloat("Camera speed", &camera.speed, 0.1f, 0.01f, 1000.0f,
                                         "%.2f", ImGuiSliderFlags_AlwaysClamp);
        result.edited |= ImGui::DragFloat("Camera sprint multiplier", &camera.sprintMultiplier, 0.1f,
                                         1.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }

    ImGui::SeparatorText("Renderer");
    const RenderPath path = context().config->rendererConfig().renderPath;
    if (ImGui::BeginCombo("Render path", path == RenderPath::RayTracing ? "Ray tracing" : "Rasterization"))
    {
        if (ImGui::Selectable("Rasterization", path == RenderPath::Rasterization) &&
            path != RenderPath::Rasterization)
        {
            context().config->setRenderPath(RenderPath::Rasterization);
            result.rendererChanged = true;
        }
        const bool supported = context().renderer->vulkanContext().supportsRayTracing();
        ImGui::BeginDisabled(!supported);
        if (ImGui::Selectable("Ray tracing", path == RenderPath::RayTracing) && path != RenderPath::RayTracing)
        {
            context().config->setRenderPath(RenderPath::RayTracing);
            result.rendererChanged = true;
        }
        ImGui::EndDisabled();
        if (!supported) ImGui::TextUnformatted("Ray tracing is not supported by this GPU");
        ImGui::EndCombo();
    }
    ImGui::Spacing();

    RenderFeature feature = context().config->rendererConfig().feature;
    if (ImGui::Checkbox("IBL", &feature.ibl))
    {
        context().config->setRenderFeature(feature);
        result.rendererChanged = true;
    }
    if (feature.ibl)
    {
        result.environmentChanged = drawEnvironmentSelection();
    }
    else if (ImGui::DragFloat(
                 "Ambient coefficient",
                 &feature.ambientCoefficient,
                 0.01f,
                 0.0f,
                 1.0f,
                 "%.3f",
                 ImGuiSliderFlags_AlwaysClamp
             ))
    {
        context().config->setRenderFeature(feature);
    }

    ImGui::SeparatorText("HDR");
    if (ImGui::Checkbox("Enable HDR", &feature.hdr))
    {
        context().config->setRenderFeature(feature);
        result.rendererChanged = true;
    }
    ImGui::BeginDisabled(!feature.hdr);
    int toneMappingMode = static_cast<int>(feature.toneMapping.mode);
    if (ImGui::Combo("Operator", &toneMappingMode, "Khronos PBR Neutral\0Reinhard\0"))
    {
        feature.toneMapping.mode = static_cast<ToneMappingOperator>(toneMappingMode);
        context().config->setRenderFeature(feature);
    }
    if (ImGui::DragFloat("Exposure (stops)", &feature.toneMapping.exposure,
                         0.05f, -10.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
    {
        context().config->setRenderFeature(feature);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Shadows");
    if (ImGui::Checkbox("Enable shadows", &feature.shadow.enable))
    {
        context().config->setRenderFeature(feature);
        result.rendererChanged = true;
    }

    ImGui::BeginDisabled(!feature.shadow.enable);
    int shadowFilter = static_cast<int>(feature.shadow.filter);
    if (ImGui::Combo("Shadow filter", &shadowFilter, "Nearest\0Linear\0"))
    {
        feature.shadow.filter = static_cast<SamplerAsset::Filter>(shadowFilter);
        context().config->setRenderFeature(feature);
        result.rendererChanged = true;
    }

    const auto drawShadowMapSize = [&](const char* label, uint32_t& mapSize, bool cube)
    {
        const std::string shadowSize = std::to_string(mapSize);
        if (!ImGui::BeginCombo(label, shadowSize.c_str()))
            return;

        constexpr uint32_t kShadowMapSizes[] = {256, 512, 1024, 2048, 4096, 8192};
        const auto&        limits            = context().renderer->vulkanContext().properties().limits;
        for (uint32_t size : kShadowMapSizes)
        {
            if ((cube && size > limits.maxImageDimensionCube) || size > limits.maxImageDimension2D ||
                size > limits.maxFramebufferWidth || size > limits.maxFramebufferHeight)
                continue;

            const bool        selected = mapSize == size;
            const std::string label    = std::to_string(size);
            if (ImGui::Selectable(label.c_str(), selected) && !selected)
            {
                mapSize = size;
                context().config->setRenderFeature(feature);
                result.rendererChanged = true;
            }
        }
        ImGui::EndCombo();
    };
    drawShadowMapSize("Point shadow map size", feature.shadow.pointShadowMapSize, true);
    drawShadowMapSize("Rect area shadow map size", feature.shadow.rectAreaShadowMapSize, false);
    drawShadowMapSize("Directional shadow map size", feature.shadow.cascade.shadowMapSize, false);
    int cascadeCount = static_cast<int>(feature.shadow.cascade.count);
    if (ImGui::SliderInt("Shadow cascades", &cascadeCount, 1, static_cast<int>(kMaxDirectionalShadowCascades)))
    {
        feature.shadow.cascade.count = static_cast<uint32_t>(cascadeCount);
        context().config->setRenderFeature(feature);
        result.rendererChanged = true;
    }
    const float minimumShadowDistance = scene.editorCamera.nearPlane * 1.01f;
    if (ImGui::DragFloat(
            "Directional shadow distance",
            &feature.shadow.cascade.maxDistance,
            0.5f,
            minimumShadowDistance,
            100000.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        context().config->setRenderFeature(feature);
    if (ImGui::SliderFloat("Cascade split lambda", &feature.shadow.cascade.splitLambda, 0.0f, 1.0f, "%.2f"))
        context().config->setRenderFeature(feature);
    if (ImGui::DragFloat(
            "Cascade depth padding",
            &feature.shadow.cascade.depthPadding,
            0.1f,
            0.0f,
            100000.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        context().config->setRenderFeature(feature);
    if (ImGui::DragFloat(
            "Shadow bias",
            &feature.shadow.bias,
            0.0001f,
            0.0f,
            1.0f,
            "%.5f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        context().config->setRenderFeature(feature);
    }
    if (ImGui::Checkbox("Per-pixel rotation", &feature.shadow.pixelRotation))
    {
        context().config->setRenderFeature(feature);
    }
    if (ImGui::DragFloat(
            "PCF radius",
            &feature.shadow.sampleRadius,
            0.0005f,
            0.0f,
            1.0f,
            "%.4f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        context().config->setRenderFeature(feature);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Assets");
    constexpr const char* assetTypes[] = {"Texture", "Environment map", "Mesh"};
    ImGui::BeginDisabled(environmentImport.valid());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##Import asset type",
                 &importAssetType,
                 assetTypes,
                 static_cast<int>(std::size(assetTypes)));
    if (ImGui::Button("Import"))
    {
        textureSetting     = {};
        environmentSetting = {};
        meshSetting        = {};
        meshPreview.clear();
        ImGui::OpenPopup("Import Asset");
    }
    ImGui::EndDisabled();
    if (environmentImport.valid())
    {
        ImGui::SameLine();
        if (ImGui::Button("Cancel import"))
        {
            importStop.request_stop();
            importStatus = "Canceling environment import...";
        }
    }
    if (!importStatus.empty())
    {
        ImGui::TextWrapped("%s", importStatus.c_str());
    }
    drawImportDialog();

    ImGui::SeparatorText("Object");
    if (object != nullptr)
    {
        ImGui::PushID(object);
        result.edited = component_draw::drawField("Name", object->name) || result.edited;
        ComponentDrawResult changes;
        for (const auto& component : object->components)
        {
            if (!component) continue;
            changes |= drawComponent(*component);
        }
        result.edited |= changes.edited;
        ImGui::PopID();
    }
    else
    {
        ImGui::TextUnformatted("No selection");
    }

    ImGui::End();
    return result;
}

bool EditorUI::drawEnvironmentSelection()
{
    ImGui::SeparatorText("Environment");
    const auto& current = context().config->rendererConfig().environmentMap;
    const char* preview = current ? current->value.c_str() : "Select environment map...";
    bool        changed = false;

    if (ImGui::BeginCombo("Environment map", preview))
    {
        auto ids = context().assetManager->loadedIds<EnvironmentMapAsset>();
        std::ranges::sort(ids, {}, &EnvironmentMapAsset::ID::value);
        for (const EnvironmentMapAsset::ID& id : ids)
        {
            const bool selected = current && *current == id;
            if (ImGui::Selectable(id.value.c_str(), selected) && !selected)
            {
                context().config->setEnvironmentMap(id);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (!current) ImGui::TextUnformatted("Using fallback environment lighting");
    return changed;
}

void EditorUI::finishEnvironmentImport()
{
    if (!environmentImport.valid() ||
        environmentImport.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
    {
        return;
    }

    auto prepared = environmentImport.get();
    if (!prepared || importStop.stop_requested())
    {
        importStatus = "Environment import canceled";
        return;
    }

    const EnvironmentMapAsset::ID id = AssetImporter::saveEnvironmentMap(
        std::move(*prepared));
    importStatus = "Imported: " + id.value;
}

void EditorUI::drawImportDialog()
{
    if (!ImGui::BeginPopupModal("Import Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    static constexpr wchar_t imageFilter[] =
        L"Image files (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.pic;*.pnm;*.exr;*.hdr)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.psd;*.pic;*.pnm;*.exr;*.hdr\0"
        L"All files (*.*)\0*.*\0";
    static constexpr wchar_t meshFilter[] =
        L"glTF files (*.gltf;*.glb)\0*.gltf;*.glb\0All files (*.*)\0*.*\0";

    std::filesystem::path* source = nullptr;
    switch (importAssetType)
    {
        case 0:
            source = &textureSetting.source;
            drawSourcePath(*source, imageFilter);
            drawColorSpace(textureSetting.colorSpace);
            break;
        case 1:
            source = &environmentSetting.source;
            drawSourcePath(*source, imageFilter);
            drawColorSpace(environmentSetting.colorSpace);
            drawPositiveUint("Irradiance face size", environmentSetting.irradianceSize);
            drawPositiveUint("Irradiance samples", environmentSetting.irradianceSampleCount);
            drawPositiveUint("Prefilter first mip samples",
                             environmentSetting.prefilteredSpecularSampleCount);
            drawPositiveUint("Prefilter sample limit", environmentSetting.prefilteredSpecularMaxSampleCount);
            break;
        case 2:
            {
                source                    = &meshSetting.source;
                const auto previousSource = *source;
                drawSourcePath(*source, meshFilter);
                if (*source != previousSource)
                {
                    meshPreview.clear();
                    meshSetting.id = source->stem().string();
                }
                if (sourceIsImportable(*source, importAssetType) && !meshPreview.ready())
                    meshPreview.load(*source);

                ImGui::InputText("Mesh ID", &meshSetting.id.value);
                ImGui::InputFloat("Import scale", &meshSetting.importScale, 0.1f, 1.0f, "%.6g");
                if (meshPreview.ready())
                {
                    const auto& prepared    = meshPreview.prepared();
                    const auto  size        = prepared.boundsMax - prepared.boundsMin;
                    const float longestSide = std::max({size.x, size.y, size.z});
                    float       targetSize  = longestSide * meshSetting.importScale;
                    if (ImGui::InputFloat("Target longest side (m)", &targetSize, 0.1f, 1.0f, "%.6g") &&
                        std::isfinite(targetSize) && targetSize > 0.0f && longestSide > 0.0f)
                        meshSetting.importScale = targetSize / longestSide;
                }
                meshPreview.draw(meshSetting.importScale);
                break;
            }
        default:
            DCHECK(false, "invalid import asset type");
    }

    const bool  validSource     = sourceIsImportable(*source, importAssetType);
    std::string sourceExtension = source->extension().string();
    std::ranges::transform(
        sourceExtension,
        sourceExtension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
    );
    const bool validColorSpace = (sourceExtension != ".exr" && sourceExtension != ".hdr") ||
        (importAssetType == 0       ? textureSetting.colorSpace != ColorSpace::Srgb
             : importAssetType == 1 ? environmentSetting.colorSpace != ColorSpace::Srgb
                                    : true);
    const bool validSettings = importAssetType == 2
        ? meshPreview.ready() && asset_import::validId(meshSetting.id.value) &&
            std::isfinite(meshSetting.importScale) && meshSetting.importScale > 0.0f
        : importAssetType != 1 ||
            (environmentSetting.irradianceSize > 0 && environmentSetting.irradianceSampleCount > 0 &&
             environmentSetting.prefilteredSpecularSampleCount > 0 &&
             environmentSetting.prefilteredSpecularMaxSampleCount >= environmentSetting.prefilteredSpecularSampleCount);
    if (!validSource)
    {
        ImGui::TextWrapped("Select an existing file in a supported format.");
    }
    else if (!validSettings || !validColorSpace)
    {
        ImGui::TextWrapped(
            "Check import settings. Mesh ID must be a valid filename; import scale must be finite and positive; "
            "EXR and HDR require Linear; sizes and sample counts must be positive, and the prefilter limit must cover "
            "the first mip."
        );
    }

    ImGui::BeginDisabled(!validSource || !validSettings || !validColorSpace);
    if (ImGui::Button("Confirm"))
    {
        std::string importedId;
        switch (importAssetType)
        {
            case 0:
                importedId = AssetImporter::importTexture(textureSetting).value;
                break;
            case 1:
                importStop = std::stop_source{};
                environmentImport = std::async(
                    std::launch::async,
                    [setting = environmentSetting, stop = importStop.get_token()]
                    {
                        return AssetImporter::prepareEnvironmentMap(setting, stop);
                    });
                importStatus = "Importing environment map (progress in console)...";
                break;
            case 2:
                importedId = AssetImporter::saveMesh(std::move(meshPreview.prepared()), meshSetting).value;
                meshPreview.clear();
                break;
            default:
                DCHECK(false, "invalid import asset type");
        }
        if (!environmentImport.valid())
        {
            importStatus = "Imported: " + importedId;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        meshPreview.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorUI::endFrame()
{
    ImGui::Render();
}

void EditorUI::render(VkCommandBuffer commandBuffer) const
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

bool EditorUI::sceneClicked(glm::vec2& mousePosition) const
{
    const ImVec2 mouse  = ImGui::GetMousePos();
    const bool   inside = mouse.x >= scenePosition.x && mouse.y >= scenePosition.y &&
        mouse.x < scenePosition.x + sceneSize.x &&
        mouse.y < scenePosition.y + sceneSize.y;
    if (!inside || ImGui::GetIO().WantCaptureMouse ||
        !context().inputManager->get(Command::SelectObject) ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ||
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
    const ImGuiIO& io = ImGui::GetIO();
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
    return context().inputManager->get(Command::SaveScene);
}

float EditorUI::sceneAspectRatio() const
{
    return sceneSize.x / std::max(sceneSize.y, 1.0f);
}

ViewportRect EditorUI::sceneViewportPixels() const
{
    return scenePixels;
}

void EditorUI::shutdown(bool stopImport) noexcept
{
    meshPreview.clear();
    if (stopImport && environmentImport.valid())
    {
        importStop.request_stop();
        environmentImport.wait();
        environmentImport = {};
    }

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
    editorDockId          = 0;
    hierarchyDockId       = 0;
    renameActorId         = 0;
    focusRename           = false;
    renameText.clear();
}
