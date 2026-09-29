#include "editor/EditorUi.h"

#include "asset/Asset.h"
#include "asset/AssetDescManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "ecs/Transform.h"
#include "editor/ComponentDraw.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
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
            const std::wstring initialDirectory =
                context().config->paths().assets.parent_path().wstring();

            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = GetActiveWindow();
            dialog.lpstrFilter = filter;
            dialog.lpstrFile = selectedFile.data();
            dialog.nMaxFile = static_cast<DWORD>(selectedFile.size());
            dialog.lpstrInitialDir = initialDirectory.c_str();
            dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
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
        std::ranges::transform(extension, extension.begin(),
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

EditorUI::InspectorResult EditorUI::drawInspector(
    Scene::Desc& scene, Scene::Desc::Object* object, bool dirty)
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
    ImGui::Spacing();
    result.environmentChanged = drawEnvironmentSelection(scene);
    result.edited = result.environmentChanged;

    ImGui::SeparatorText("Assets");
    constexpr const char* assetTypes[] = {"Texture", "Environment map", "Mesh"};
    ImGui::BeginDisabled(environmentImport.valid());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##Import asset type", &importAssetType, assetTypes,
                 static_cast<int>(std::size(assetTypes)));
    if (ImGui::Button("Import"))
    {
        textureSetting = {};
        environmentSetting = {};
        meshSetting = {};
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
        ImGui::Text("Selected: %s", object->name.c_str());
        for (auto& [typeName, component] : object->components)
        {
            ImGui::Spacing();
            ImGui::PushID(typeName.c_str());
            result.edited = drawComponent(typeName, component) || result.edited;
            ImGui::PopID();
        }
    }
    else
    {
        ImGui::TextUnformatted("No selection");
    }

    ImGui::End();
    return result;
}

bool EditorUI::drawEnvironmentSelection(Scene::Desc& scene)
{
    ImGui::SeparatorText("Scene environment");
    const auto& current = scene.environment.environmentMap;
    const char* preview = current ? current->value.c_str() : "None";
    bool changed = false;

    if (ImGui::BeginCombo("Environment map", preview))
    {
        if (ImGui::Selectable("None", !current) && current)
        {
            scene.environment.environmentMap.reset();
            changed = true;
        }

        auto ids = context().assetDescManager->loadedIds<EnvironmentMap>();
        std::ranges::sort(ids, {}, &EnvironmentMap::ID::value);
        for (const EnvironmentMap::ID& id : ids)
        {
            const bool selected = current && *current == id;
            if (ImGui::Selectable(id.value.c_str(), selected) && !selected)
            {
                scene.environment.environmentMap = id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void EditorUI::finishEnvironmentImport()
{
    if (!environmentImport.valid() ||
        environmentImport.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
    {
        return;
    }

    try
    {
        auto prepared = environmentImport.get();
        if (!prepared || importStop.stop_requested())
        {
            importStatus = "Environment import canceled";
            return;
        }

        const EnvironmentMap::ID id = AssetImporter::saveEnvironmentMap(
            std::move(*prepared));
        importStatus = "Imported: " + id.value;
    }
    catch (const std::exception& error)
    {
        LOG_ERROR("Environment import failed: {}", error.what());
        importStatus = std::string{"Import failed: "} + error.what();
    }
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
            drawPositiveUint("Prefilter sample limit",
                             environmentSetting.prefilteredSpecularMaxSampleCount);
            break;
        case 2:
            source = &meshSetting.source;
            drawSourcePath(*source, meshFilter);
            break;
        default:
            CHECK(false, "invalid import asset type");
    }

    const bool validSource = sourceIsImportable(*source, importAssetType);
    std::string sourceExtension = source->extension().string();
    std::ranges::transform(sourceExtension, sourceExtension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    const bool validColorSpace = (sourceExtension != ".exr" && sourceExtension != ".hdr") ||
        (importAssetType == 0
            ? textureSetting.colorSpace != ColorSpace::Srgb
            : importAssetType == 1
                ? environmentSetting.colorSpace != ColorSpace::Srgb
                : true);
    const bool validSettings = importAssetType != 1 ||
        (environmentSetting.irradianceSize > 0 &&
         environmentSetting.irradianceSampleCount > 0 &&
         environmentSetting.prefilteredSpecularSampleCount > 0 &&
         environmentSetting.prefilteredSpecularMaxSampleCount >=
             environmentSetting.prefilteredSpecularSampleCount);
    if (!validSource)
    {
        ImGui::TextWrapped("Select an existing file in a supported format.");
    }
    else if (!validSettings || !validColorSpace)
    {
        ImGui::TextWrapped("Check color space and sample settings. EXR and HDR require Linear; sizes and sample counts must be positive, and the prefilter limit must cover the first mip.");
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
                importedId = AssetImporter::importMesh(meshSetting).value;
                break;
            default:
                CHECK(false, "invalid import asset type");
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

void EditorUI::shutdown(bool stopImport) noexcept
{
    if (stopImport && environmentImport.valid())
    {
        importStop.request_stop();
        environmentImport.wait();
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
    editorDockId = 0;
}
