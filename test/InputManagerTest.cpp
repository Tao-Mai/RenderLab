#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Window.h"

#include <array>
#include <utility>

#include <gtest/gtest.h>

namespace
{
constexpr std::array lockStates = {
    0, GLFW_MOD_NUM_LOCK, GLFW_MOD_CAPS_LOCK, GLFW_MOD_NUM_LOCK | GLFW_MOD_CAPS_LOCK,
};

class InputManagerTest : public testing::Test
{
protected:
    ConfigManager config;
    Window window;
    InputManager input;
    Context previousContext;
    GLFWkeyfun keyCallback = nullptr;
    GLFWmousebuttonfun mouseCallback = nullptr;
    GLFWwindowfocusfun focusCallback = nullptr;

    void SetUp() override
    {
        previousContext = context();
        context().config = &config;
        context().window = &window;
        context().inputManager = &input;
        config.init();

        ASSERT_EQ(glfwInit(), GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window.init();
        input.init();

        const auto handle = window.nativeHandle();
        keyCallback = glfwSetKeyCallback(handle, nullptr);
        glfwSetKeyCallback(handle, keyCallback);
        mouseCallback = glfwSetMouseButtonCallback(handle, nullptr);
        glfwSetMouseButtonCallback(handle, mouseCallback);
        focusCallback = glfwSetWindowFocusCallback(handle, nullptr);
        glfwSetWindowFocusCallback(handle, focusCallback);

        ASSERT_NE(keyCallback, nullptr);
        ASSERT_NE(mouseCallback, nullptr);
        ASSERT_NE(focusCallback, nullptr);
    }

    void TearDown() override
    {
        input.shutdown();
        window.shutdown();
        config.shutdown();
        context() = previousContext;
    }

    void pressKey(int key, int modifiers)
    {
        keyCallback(window.nativeHandle(), key, 0, GLFW_RELEASE, modifiers);
        input.tick();
        keyCallback(window.nativeHandle(), key, 0, GLFW_PRESS, modifiers);
        input.tick();
    }

    void pressMouse(int button, int modifiers)
    {
        mouseCallback(window.nativeHandle(), button, GLFW_RELEASE, modifiers);
        input.tick();
        mouseCallback(window.nativeHandle(), button, GLFW_PRESS, modifiers);
        input.tick();
    }
};

TEST_F(InputManagerTest, MouseSelectionIgnoresLockStates)
{
    for (const int locks : lockStates)
    {
        SCOPED_TRACE(locks);
        pressMouse(GLFW_MOUSE_BUTTON_LEFT, locks);
        EXPECT_TRUE(input.get(Command::SelectObject));

        input.tick();
        EXPECT_FALSE(input.get(Command::SelectObject));
    }
}

TEST_F(InputManagerTest, KeyboardShortcutsIgnoreLockStates)
{
    constexpr std::array shortcuts = {
        std::pair{GLFW_KEY_ESCAPE, Command::Quit},
        std::pair{GLFW_KEY_V, Command::ToggleFreeMovement},
        std::pair{GLFW_KEY_W, Command::GizmoTranslate},
        std::pair{GLFW_KEY_E, Command::GizmoRotate},
        std::pair{GLFW_KEY_R, Command::GizmoScale},
    };
    for (const auto& [key, command] : shortcuts)
    {
        for (const int locks : lockStates)
        {
            SCOPED_TRACE(key);
            SCOPED_TRACE(locks);
            pressKey(key, locks);
            EXPECT_TRUE(input.get(command));

            input.tick();
            EXPECT_FALSE(input.get(command));
        }
    }
}

TEST_F(InputManagerTest, ControlShortcutStillMatchesModifiersExactly)
{
    constexpr std::array modifiers = {
        0, GLFW_MOD_CONTROL, GLFW_MOD_SHIFT, GLFW_MOD_ALT, GLFW_MOD_SUPER,
        GLFW_MOD_CONTROL | GLFW_MOD_SHIFT,
        GLFW_MOD_CONTROL | GLFW_MOD_ALT,
        GLFW_MOD_CONTROL | GLFW_MOD_SUPER,
    };
    for (const int modifier : modifiers)
    {
        for (const int locks : lockStates)
        {
            SCOPED_TRACE(modifier | locks);
            pressKey(GLFW_KEY_S, modifier | locks);
            EXPECT_EQ(input.get(Command::SaveScene), modifier == GLFW_MOD_CONTROL);
        }
    }
}

TEST_F(InputManagerTest, MouseSelectionStillRejectsOtherModifiers)
{
    for (const int modifier : {GLFW_MOD_SHIFT, GLFW_MOD_CONTROL, GLFW_MOD_ALT, GLFW_MOD_SUPER})
    {
        for (const int locks : lockStates)
        {
            SCOPED_TRACE(modifier | locks);
            pressMouse(GLFW_MOUSE_BUTTON_LEFT, modifier | locks);
            EXPECT_FALSE(input.get(Command::SelectObject));
        }
    }
}

TEST_F(InputManagerTest, NavigationHoldAndFocusLossAreUnchanged)
{
    for (const int locks : lockStates)
    {
        SCOPED_TRACE(locks);
        pressMouse(GLFW_MOUSE_BUTTON_RIGHT, locks);
        EXPECT_TRUE(input.get(Command::Navigate));
        input.tick();
        EXPECT_TRUE(input.get(Command::Navigate));

        mouseCallback(window.nativeHandle(), GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, locks);
        input.tick();
        EXPECT_FALSE(input.get(Command::Navigate));

        pressMouse(GLFW_MOUSE_BUTTON_RIGHT, locks);
        focusCallback(window.nativeHandle(), GLFW_FALSE);
        input.tick();
        EXPECT_FALSE(input.get(Command::Navigate));
        EXPECT_FALSE(input.get(Command::SelectObject));
    }
}
}
