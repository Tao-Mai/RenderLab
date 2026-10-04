#pragma once

#include "asset/Asset.h"
#include "asset/AssetImporter.h"
#include "editor/ViewportRect.h"

#include <cstdint>
#include <future>
#include <optional>
#include <stop_token>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <vulkan/vulkan.h>

struct GLFWwindow;
struct TransformComponent;

class EditorUI
{
public:
    struct InspectorResult
    {
        bool edited = false;
        bool environmentChanged = false;
        bool resourcesChanged = false;
        bool rendererChanged = false;
    };

    EditorUI() = default;
    ~EditorUI();

    EditorUI(const EditorUI&)            = delete;
    EditorUI& operator=(const EditorUI&) = delete;

    void init(
        GLFWwindow*      window,
        VkInstance       instance,
        VkPhysicalDevice physicalDevice,
        VkDevice         device,
        uint32_t         queueFamily,
        VkQueue          queue,
        VkFormat         colorFormat,
        uint32_t         minImageCount,
        uint32_t         imageCount);
    void beginFrame(float deltaTime);
    [[nodiscard]] bool drawGizmo(
        TransformComponent&  transform,
        const glm::mat4& view,
        const glm::mat4& projection,
        bool             enableShortcuts);
    [[nodiscard]] InspectorResult drawInspector(
        SceneAsset& scene,
        AActor* object,
        bool             dirty);
    void endFrame();
    void render(VkCommandBuffer commandBuffer) const;
    void shutdown(bool stopImport = true) noexcept;

    [[nodiscard]] bool         sceneClicked(glm::vec2& mousePosition) const;
    [[nodiscard]] bool         wantsInput() const;
    [[nodiscard]] bool         saveRequested() const;
    [[nodiscard]] float        sceneAspectRatio() const;
    [[nodiscard]] ViewportRect sceneViewportPixels() const;

private:
    bool                              contextCreated            = false;
    bool                              glfwBackendInitialized    = false;
    bool                              vulkanBackendInitialized  = false;
    bool                              dockLayoutInitialized     = false;
    float                             fpsRefreshTime            = 0.0f;
    float                             displayedFps              = 0.0f;
    uint32_t                          framesSinceRefresh        = 0;
    uint32_t                          editorDockId              = 0;
    int                               gizmoOperation            = 0;
    glm::vec2                         scenePosition{0.0f};
    glm::vec2                         sceneSize{1.0f};
    ViewportRect                      scenePixels;
    VkFormat                          colorFormat = VK_FORMAT_UNDEFINED;
    VkPipelineRenderingCreateInfoKHR  pipelineRenderingInfo{};
    int                               importAssetType = 0;
    ImportTextureSetting              textureSetting;
    ImportEnvironmentMapSetting       environmentSetting;
    ImportMeshSetting                 meshSetting;
    std::string                       importStatus;
    std::stop_source                  importStop;
    std::future<std::optional<AssetImporter::PreparedEnvironmentMap>> environmentImport;

    void drawImportDialog();
    void finishEnvironmentImport();
    [[nodiscard]] bool drawEnvironmentSelection(SceneAsset& scene);
};
