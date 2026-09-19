#include "window.h"

#include <stdexcept>

Window::~Window()
{
    shutdown();
}

void Window::initialize(int width, int height, const char *title)
{
    if (handle != nullptr)
    {
        return;
    }

    if (glfwInit() != GLFW_TRUE)
    {
        throw std::runtime_error("failed to initialize GLFW");
    }
    glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (handle == nullptr)
    {
        shutdown();
        throw std::runtime_error("failed to create GLFW window");
    }
}

void Window::pollEvents() const
{
    glfwPollEvents();
}

bool Window::shouldClose() const
{
    return handle == nullptr || glfwWindowShouldClose(handle);
}

VkSurfaceKHR Window::createVulkanSurface(VkInstance instance) const
{
    if (handle == nullptr)
    {
        throw std::logic_error("window is not initialized");
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, handle, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create Vulkan surface");
    }
    return surface;
}

std::vector<const char *> Window::requiredVulkanExtensions() const
{
    uint32_t extensionCount = 0;
    const char **extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr)
    {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }

    return {extensions, extensions + extensionCount};
}

std::pair<int, int> Window::framebufferSize() const
{
    if (handle == nullptr)
    {
        throw std::logic_error("window is not initialized");
    }

    int width  = 0;
    int height = 0;
    glfwGetFramebufferSize(handle, &width, &height);
    return {width, height};
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

