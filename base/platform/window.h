#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>

class GlfwWindow
{
public:
    enum class WindowState : uint8_t
    {
        Normal = 0,
        Maximized,
        Fullscreen,
    };

    GlfwWindow(int width, int height, const std::string& title, WindowState state = WindowState::Normal);
    ~GlfwWindow();

    GlfwWindow(const GlfwWindow&)            = delete;
    GlfwWindow& operator=(const GlfwWindow&) = delete;

    bool ShouldClose() { return glfwWindowShouldClose(window_); }
    void PollEvents() { glfwPollEvents(); }
    void SwapBuffer() { glfwSwapBuffers(window_); }
    void SetClose() { glfwSetWindowShouldClose(window_, true); }

    // 相机拖拽时隐藏光标
    void SetCursorVisible(bool visible)
    {
        if (cursor_visible_ == visible)
            return;
        glfwSetInputMode(window_, GLFW_CURSOR,
                         visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
        cursor_visible_ = visible;
    }

    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }
    [[nodiscard]] float Aspect() const
    {
        return height_ > 0 ? static_cast<float>(width_) / static_cast<float>(height_) : 1.0f;
    }

    GLFWwindow* GetNativeWindow() { return window_; }

private:
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_        = nullptr;
    int         width_         = 0;
    int         height_        = 0;
    bool        cursor_visible_ = true;
};
