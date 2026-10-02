#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class Window
{
  public:
    using KeyCallback = std::function<void(int key, int scancode, int action, int mods)>;
    using MouseButtonCallback = std::function<void(int button, int action, int mods)>;
    using FocusCallback = std::function<void(int focused)>;

    Window() = default;
    ~Window();

    Window(const Window &)            = delete;
    Window &operator=(const Window &) = delete;

    void init();
    void pollEvents() const;
    void waitEvents() const;
    [[nodiscard]] bool shouldClose() const;
    void requestClose() const;
    void setKeyCallback(KeyCallback callback);
    void setMouseButtonCallback(MouseButtonCallback callback);
    void setFocusCallback(FocusCallback callback);

    [[nodiscard]] std::pair<double, double> cursorPosition() const;
    void setCursorCaptured(bool captured) const;
    double consumeScrollOffset();
    [[nodiscard]] VkSurfaceKHR createVulkanSurface(VkInstance instance) const;
    [[nodiscard]] std::vector<const char *> requiredVulkanExtensions() const;
    [[nodiscard]] std::pair<int, int> framebufferSize() const;
    [[nodiscard]] GLFWwindow *nativeHandle() const;
    void shutdown() noexcept;

  private:
    bool        glfwInitialized = false;
    GLFWwindow *handle          = nullptr;
    double      scrollOffset    = 0.0;

    KeyCallback onKey;
    MouseButtonCallback onMouseButton;
    FocusCallback onFocus;

    static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
    static void focusCallback(GLFWwindow *window, int focused);
    static void scrollCallback(GLFWwindow *window, double xOffset, double yOffset);
};
