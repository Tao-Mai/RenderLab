#include "core/Window.h"

#include "core/Logger.h"
#include "core/ConfigManager.h"
#include "core/Context.h"

#include <algorithm>
#include <cstdint>

namespace
{
struct WorkArea
{
    int x;
    int y;
    int width;
    int height;
};

WorkArea monitorWorkArea(GLFWmonitor* monitor)
{
    WorkArea area{};
    glfwGetMonitorWorkarea(monitor, &area.x, &area.y, &area.width, &area.height);
    return area;
}

WorkArea windowWorkArea(GLFWwindow* window, int width, int height)
{
    int x = 0;
    int y = 0;
    glfwGetWindowPos(window, &x, &y);

    auto* primary = glfwGetPrimaryMonitor();
    CHECK(primary);
    auto area = monitorWorkArea(primary);
    int monitorCount = 0;
    auto* monitors = glfwGetMonitors(&monitorCount);
    int64_t largestOverlap = 0;
    for (int index = 0; index < monitorCount; ++index)
    {
        const auto candidate = monitorWorkArea(monitors[index]);
        const int overlapWidth = std::max(0, std::min(x + width, candidate.x + candidate.width) - std::max(x, candidate.x));
        const int overlapHeight = std::max(0, std::min(y + height, candidate.y + candidate.height) - std::max(y, candidate.y));
        const auto overlap = int64_t{overlapWidth} * overlapHeight;
        if (overlap > largestOverlap)
        {
            area = candidate;
            largestOverlap = overlap;
        }
    }
    return area;
}

void centerWindow(GLFWwindow* window, const WorkArea& area, int width, int height)
{
    int left = 0, top = 0, right = 0, bottom = 0;
    glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);
    glfwSetWindowPos(
        window,
        area.x + (area.width - width - left - right) / 2 + left,
        area.y + (area.height - height - top - bottom) / 2 + top
    );
}
}

Window::~Window()
{
    DCHECK(!handle && !glfwInitialized);
}

void Window::init()
{
    if (handle != nullptr)
    {
        return;
    }

    DCHECK(context().config);
    const EditorConfig& editor = context().config->editorConfig();

    CHECK(glfwInit() == GLFW_TRUE, "failed to init GLFW");
    glfwInitialized = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    handle = glfwCreateWindow(editor.width, editor.height, editor.title.c_str(), nullptr, nullptr);
    if (handle == nullptr)
    {
        shutdown();
    }
    CHECK(handle != nullptr, "failed to create GLFW window");

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    CHECK(monitor);
    const auto [width, height] = size();
    context().config->setEditorSize(width, height);
    centerWindow(handle, monitorWorkArea(monitor), width, height);

    glfwSetInputMode(handle, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    glfwSetWindowUserPointer(handle, this);
    glfwSetKeyCallback(handle, &Window::keyCallback);
    glfwSetMouseButtonCallback(handle, &Window::mouseButtonCallback);
    glfwSetWindowFocusCallback(handle, &Window::focusCallback);
    glfwSetWindowSizeCallback(handle, &Window::sizeCallback);
    glfwSetScrollCallback(handle, &Window::scrollCallback);
}

void Window::pollEvents() const
{
    glfwPollEvents();
}

void Window::waitEvents() const
{
    glfwWaitEvents();
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

void Window::setKeyCallback(KeyCallback callback)
{
    DCHECK(handle);
    DCHECK(!callback || !onKey, "key callback is already registered");

    onKey = std::move(callback);
}

void Window::setMouseButtonCallback(MouseButtonCallback callback)
{
    DCHECK(handle);
    DCHECK(!callback || !onMouseButton,
        "mouse button callback is already registered");

    onMouseButton = std::move(callback);
}

void Window::setFocusCallback(FocusCallback callback)
{
    DCHECK(handle);
    DCHECK(!callback || !onFocus, "focus callback is already registered");

    onFocus = std::move(callback);
}

std::pair<double, double> Window::cursorPosition() const
{
    DCHECK(handle);
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
    DCHECK(handle);

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

std::pair<int, int> Window::size() const
{
    DCHECK(handle);

    int width = 0;
    int height = 0;
    glfwGetWindowSize(handle, &width, &height);
    return {width, height};
}

void Window::resizeCentered(int width, int height) const
{
    DCHECK(handle && width > 0 && height > 0);

    const auto [oldWidth, oldHeight] = size();
    const auto area = windowWorkArea(handle, oldWidth, oldHeight);
    if (glfwGetWindowAttrib(handle, GLFW_MAXIMIZED))
        glfwRestoreWindow(handle);
    glfwSetWindowSize(handle, width, height);

    const auto [actualWidth, actualHeight] = size();
    centerWindow(handle, area, actualWidth, actualHeight);
}

std::pair<int, int> Window::framebufferSize() const
{
    DCHECK(handle);

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

    onKey = {};
    onMouseButton = {};
    onFocus = {};

    if (glfwInitialized)
    {
        glfwTerminate();
        glfwInitialized = false;
    }
}

void Window::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    auto *owner = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (owner != nullptr && owner->onKey)
    {
        owner->onKey(key, scancode, action, mods);
    }
}

void Window::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    auto *owner = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (owner != nullptr && owner->onMouseButton)
    {
        owner->onMouseButton(button, action, mods);
    }
}

void Window::focusCallback(GLFWwindow *window, int focused)
{
    auto *owner = static_cast<Window *>(glfwGetWindowUserPointer(window));
    if (owner != nullptr && owner->onFocus)
    {
        owner->onFocus(focused);
    }
}

void Window::sizeCallback(GLFWwindow *window, int width, int height)
{
    DCHECK(glfwGetWindowUserPointer(window));

    // A minimized window must not persist a zero size.
    if (width > 0 && height > 0)
        context().config->setEditorSize(width, height);
}

void Window::scrollCallback(GLFWwindow *window, double, double yOffset)
{
    if (auto *owner = static_cast<Window *>(glfwGetWindowUserPointer(window)))
    {
        owner->scrollOffset += yOffset;
    }
}
