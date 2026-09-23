#include "window.h"

#include "logger.h"

Window::~Window()
{
    shutdown();
}

void Window::init()
{
    if (handle != nullptr)
    {
        return;
    }

    constexpr int width = 1440;
    constexpr int height = 900;
    constexpr const char* title = "RenderLab";

    CHECK(glfwInit() == GLFW_TRUE, "failed to init GLFW");
    glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (handle == nullptr)
    {
        shutdown();
    }
    CHECK(handle != nullptr, "failed to create GLFW window");

    glfwSetWindowUserPointer(handle, this);
    glfwSetScrollCallback(handle, &Window::scrollCallback);
}

void Window::pollEvents() const
{
    glfwPollEvents();
}

bool Window::shouldClose() const
{
    return handle == nullptr || glfwWindowShouldClose(handle);
}

void Window::requestClose() const
{
    if (handle != nullptr)
    {
        glfwSetWindowShouldClose(handle, GLFW_TRUE);
    }
}

bool Window::keyPressed(int key) const
{
    return handle != nullptr && glfwGetKey(handle, key) == GLFW_PRESS;
}

bool Window::mouseButtonPressed(int button) const
{
    return handle != nullptr && glfwGetMouseButton(handle, button) == GLFW_PRESS;
}

std::pair<double, double> Window::cursorPosition() const
{
    CHECK(handle != nullptr, "window is not initialized");
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(handle, &x, &y);
    return {x, y};
}

void Window::setCursorCaptured(bool captured) const
{
    if (handle == nullptr)
    {
        return;
    }
    glfwSetInputMode(handle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported() == GLFW_TRUE)
    {
        glfwSetInputMode(handle, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }
}

double Window::consumeScrollOffset()
{
    const double value = scrollOffset;
    scrollOffset = 0.0;
    return value;
}

VkSurfaceKHR Window::createVulkanSurface(VkInstance instance) const
{
    CHECK(handle != nullptr, "window is not initialized");

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    CHECK(glfwCreateWindowSurface(instance, handle, nullptr, &surface) == VK_SUCCESS,
        "failed to create Vulkan surface");
    return surface;
}

std::vector<const char *> Window::requiredVulkanExtensions() const
{
    uint32_t extensionCount = 0;
    const char **extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    CHECK(extensions != nullptr, "GLFW did not provide Vulkan instance extensions");

    return {extensions, extensions + extensionCount};
}

std::pair<int, int> Window::framebufferSize() const
{
    CHECK(handle != nullptr, "window is not initialized");

    int width  = 0;
    int height = 0;
    glfwGetFramebufferSize(handle, &width, &height);
    return {width, height};
}

GLFWwindow *Window::nativeHandle() const
{
    return handle;
}

void Window::shutdown() noexcept
{
    if (handle != nullptr)
    {
        glfwDestroyWindow(handle);
        handle = nullptr;
    }

    if (glfwInitialized)
    {
        glfwTerminate();
        glfwInitialized = false;
    }
}

void Window::scrollCallback(GLFWwindow *window, double, double yOffset)
{
    if (auto *owner = static_cast<Window *>(glfwGetWindowUserPointer(window)))
    {
        owner->scrollOffset += yOffset;
    }
}
