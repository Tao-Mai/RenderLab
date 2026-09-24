#pragma once

#include "editor/ViewportRect.h"

#include <cstdint>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <vulkan/vulkan.h>

struct GLFWwindow;
namespace ecs { struct Transform; }
struct SceneObjectDesc;

class EditorUI
{
public:
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
        ecs::Transform&  transform,
        const glm::mat4& view,
        const glm::mat4& projection,
        bool             enableShortcuts);
    [[nodiscard]] bool drawInspector(
        SceneObjectDesc* object,
        std::string_view sceneName,
        bool             dirty);
    void endFrame();
    void render(VkCommandBuffer commandBuffer) const;
    void shutdown() noexcept;

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
};
