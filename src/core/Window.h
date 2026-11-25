#pragma once

#include "core/utils/NonCopyable.h"

#include <utility>

struct GLFWwindow;

class Window : private NonCopyable
{
public:
    Window() = default;
    ~Window();


    void init(int width, int height, const char* title);
    void shutdown() noexcept;

    static void pollEvents();
    static void waitEvents();
    [[nodiscard]] bool shouldClose() const;
    void requestClose() const;

    [[nodiscard]] std::pair<int, int> size() const;
    [[nodiscard]] std::pair<int, int> framebufferSize() const;
    void resizeCentered(int width, int height) const;

    [[nodiscard]] std::pair<double, double> cursorPosition() const;
    void setCursorCaptured(bool captured) const;

    [[nodiscard]] GLFWwindow* nativeHandle() const { return handle; }

private:
    GLFWwindow* handle = nullptr;
};
