#include "core/Window.h"
#include "core/Logger.h"
#include <GLFW/glfw3.h>

Window::~Window() { shutdown(); }

void Window::init(int width, int height, const char* title)
{
    CHECK(glfwInit() == GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    CHECK(handle, "cannot create {} x {} GLFW window", width, height);

    const auto [actualWidth, actualHeight] = size();
    CHECK(actualWidth > 0 && actualHeight > 0);

    auto* monitor = glfwGetPrimaryMonitor();
    CHECK(monitor);

    int workX, workY, workWidth, workHeight;
    glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);
    int left, top, right, bottom;
    glfwGetWindowFrameSize(handle, &left, &top, &right, &bottom);
    glfwSetWindowPos(
        handle,
        workX + (workWidth - actualWidth - left - right) / 2 + left,
        workY + (workHeight - actualHeight - top - bottom) / 2 + top
    );
}

void Window::shutdown() noexcept
{
    if (!handle)
        return;

    glfwDestroyWindow(handle);
    handle = nullptr;
    glfwTerminate();
}

void Window::pollEvents() { glfwPollEvents(); }

void Window::waitEvents() { glfwWaitEvents(); }

bool Window::shouldClose() const { return glfwWindowShouldClose(handle) == GLFW_TRUE; }

void Window::requestClose() const { glfwSetWindowShouldClose(handle, GLFW_TRUE); }

std::pair<int, int> Window::size() const
{
    int width, height;
    glfwGetWindowSize(handle, &width, &height);

    return {width, height};
}

std::pair<int, int> Window::framebufferSize() const
{
    int width, height;
    glfwGetFramebufferSize(handle, &width, &height);

    return {width, height};
}

void Window::resizeCentered(int width, int height) const
{
    const auto [oldWidth, oldHeight] = size();
    int x, y;
    glfwGetWindowPos(handle, &x, &y);

    if (glfwGetWindowAttrib(handle, GLFW_MAXIMIZED) == GLFW_TRUE)
        glfwRestoreWindow(handle);
    glfwSetWindowSize(handle, width, height);

    const auto [actualWidth, actualHeight] = size();
    glfwSetWindowPos(handle, x + (oldWidth - actualWidth) / 2, y + (oldHeight - actualHeight) / 2);
}

std::pair<double, double> Window::cursorPosition() const
{
    double x, y;
    glfwGetCursorPos(handle, &x, &y);

    return {x, y};
}

void Window::setCursorCaptured(bool captured) const
{
    glfwSetInputMode(handle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
        glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
}
