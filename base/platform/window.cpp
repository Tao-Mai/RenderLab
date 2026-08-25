#include "base/platform/window.h"

#include <glog/logging.h>

void GlfwWindow::framebuffer_size_callback(GLFWwindow* window, const int width, const int height)
{
    if (auto* self = static_cast<GlfwWindow*>(glfwGetWindowUserPointer(window)))
    {
        self->width_  = width;
        self->height_ = height;
        glViewport(0, 0, width, height);
    }
}

GlfwWindow::GlfwWindow(const int width, const int height, const std::string& title, WindowState state) :
    width_(width), height_(height)
{
    GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode    = monitor ? glfwGetVideoMode(monitor) : nullptr;

    if (state == WindowState::Fullscreen && mode)
    {
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        window_ = glfwCreateWindow(mode->width, mode->height, title.c_str(), monitor, nullptr);
        width_  = mode->width;
        height_ = mode->height;
    }
    else
    {
        if (state == WindowState::Maximized)
            glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
        window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    }

    CHECK(window_) << "Failed to create GLFW window";

    glfwMakeContextCurrent(window_);

    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        LOG(FATAL) << "Failed to initialize GLAD";

    LOG(INFO) << "OpenGL Version: " << glGetString(GL_VERSION);

    if (mode)
    {
        int window_w = 0;
        int window_h = 0;
        glfwGetWindowSize(window_, &window_w, &window_h);
        glfwSetWindowPos(window_, (mode->width - window_w) / 2, (mode->height - window_h) / 2);
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glViewport(0, 0, width_, height_);
}

GlfwWindow::~GlfwWindow()
{
    if (window_)
        glfwDestroyWindow(window_);
}
