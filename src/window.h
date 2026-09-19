#pragma once

#include <utility>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class Window
{
  public:
    Window() = default;
    ~Window();

    Window(const Window &)            = delete;
    Window &operator=(const Window &) = delete;

    void initialize(int width, int height, const char *title);
    void pollEvents() const;
    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] bool keyPressed(int key) const;
    [[nodiscard]] bool mouseButtonPressed(int button) const;
    [[nodiscard]] std::pair<double, double> cursorPosition() const;
    void setCursorCaptured(bool captured) const;
    double consumeScrollOffset();
    [[nodiscard]] VkSurfaceKHR createVulkanSurface(VkInstance instance) const;
    [[nodiscard]] std::vector<const char *> requiredVulkanExtensions() const;
    [[nodiscard]] std::pair<int, int> framebufferSize() const;
    void shutdown() noexcept;

  private:
    bool        glfwInitialized = false;
    GLFWwindow *handle          = nullptr;
    double      scrollOffset    = 0.0;

    static void scrollCallback(GLFWwindow *window, double xOffset, double yOffset);
};
