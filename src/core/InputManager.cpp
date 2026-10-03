#include "core/InputManager.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"

#include <array>
#include <utility>

namespace
{
constexpr auto keyMap = []
{
    std::array<Key, GLFW_KEY_LAST + 1> map{};
    map.fill(Key::Unknown);
    constexpr std::array entries = {
        std::pair{GLFW_KEY_SPACE, Key::Space}, std::pair{GLFW_KEY_APOSTROPHE, Key::Apostrophe},
        std::pair{GLFW_KEY_COMMA, Key::Comma}, std::pair{GLFW_KEY_MINUS, Key::Minus},
        std::pair{GLFW_KEY_PERIOD, Key::Period}, std::pair{GLFW_KEY_SLASH, Key::Slash},
        std::pair{GLFW_KEY_SEMICOLON, Key::Semicolon}, std::pair{GLFW_KEY_EQUAL, Key::Equal},
        std::pair{GLFW_KEY_LEFT_BRACKET, Key::LeftBracket}, std::pair{GLFW_KEY_BACKSLASH, Key::Backslash},
        std::pair{GLFW_KEY_RIGHT_BRACKET, Key::RightBracket}, std::pair{GLFW_KEY_GRAVE_ACCENT, Key::GraveAccent},
        std::pair{GLFW_KEY_0, Key::Digit0}, std::pair{GLFW_KEY_1, Key::Digit1},
        std::pair{GLFW_KEY_2, Key::Digit2}, std::pair{GLFW_KEY_3, Key::Digit3},
        std::pair{GLFW_KEY_4, Key::Digit4}, std::pair{GLFW_KEY_5, Key::Digit5},
        std::pair{GLFW_KEY_6, Key::Digit6}, std::pair{GLFW_KEY_7, Key::Digit7},
        std::pair{GLFW_KEY_8, Key::Digit8}, std::pair{GLFW_KEY_9, Key::Digit9},
        std::pair{GLFW_KEY_A, Key::A}, std::pair{GLFW_KEY_B, Key::B},
        std::pair{GLFW_KEY_C, Key::C}, std::pair{GLFW_KEY_D, Key::D},
        std::pair{GLFW_KEY_E, Key::E}, std::pair{GLFW_KEY_F, Key::F},
        std::pair{GLFW_KEY_G, Key::G}, std::pair{GLFW_KEY_H, Key::H},
        std::pair{GLFW_KEY_I, Key::I}, std::pair{GLFW_KEY_J, Key::J},
        std::pair{GLFW_KEY_K, Key::K}, std::pair{GLFW_KEY_L, Key::L},
        std::pair{GLFW_KEY_M, Key::M}, std::pair{GLFW_KEY_N, Key::N},
        std::pair{GLFW_KEY_O, Key::O}, std::pair{GLFW_KEY_P, Key::P},
        std::pair{GLFW_KEY_Q, Key::Q}, std::pair{GLFW_KEY_R, Key::R},
        std::pair{GLFW_KEY_S, Key::S}, std::pair{GLFW_KEY_T, Key::T},
        std::pair{GLFW_KEY_U, Key::U}, std::pair{GLFW_KEY_V, Key::V},
        std::pair{GLFW_KEY_W, Key::W}, std::pair{GLFW_KEY_X, Key::X},
        std::pair{GLFW_KEY_Y, Key::Y}, std::pair{GLFW_KEY_Z, Key::Z},
        std::pair{GLFW_KEY_WORLD_1, Key::World1}, std::pair{GLFW_KEY_WORLD_2, Key::World2},
        std::pair{GLFW_KEY_ESCAPE, Key::Escape}, std::pair{GLFW_KEY_ENTER, Key::Enter},
        std::pair{GLFW_KEY_TAB, Key::Tab}, std::pair{GLFW_KEY_BACKSPACE, Key::Backspace},
        std::pair{GLFW_KEY_INSERT, Key::Insert}, std::pair{GLFW_KEY_DELETE, Key::Delete},
        std::pair{GLFW_KEY_RIGHT, Key::Right}, std::pair{GLFW_KEY_LEFT, Key::Left},
        std::pair{GLFW_KEY_DOWN, Key::Down}, std::pair{GLFW_KEY_UP, Key::Up},
        std::pair{GLFW_KEY_PAGE_UP, Key::PageUp}, std::pair{GLFW_KEY_PAGE_DOWN, Key::PageDown},
        std::pair{GLFW_KEY_HOME, Key::Home}, std::pair{GLFW_KEY_END, Key::End},
        std::pair{GLFW_KEY_CAPS_LOCK, Key::CapsLock}, std::pair{GLFW_KEY_SCROLL_LOCK, Key::ScrollLock},
        std::pair{GLFW_KEY_NUM_LOCK, Key::NumLock}, std::pair{GLFW_KEY_PRINT_SCREEN, Key::PrintScreen},
        std::pair{GLFW_KEY_PAUSE, Key::Pause}, std::pair{GLFW_KEY_F1, Key::F1},
        std::pair{GLFW_KEY_F2, Key::F2}, std::pair{GLFW_KEY_F3, Key::F3},
        std::pair{GLFW_KEY_F4, Key::F4}, std::pair{GLFW_KEY_F5, Key::F5},
        std::pair{GLFW_KEY_F6, Key::F6}, std::pair{GLFW_KEY_F7, Key::F7},
        std::pair{GLFW_KEY_F8, Key::F8}, std::pair{GLFW_KEY_F9, Key::F9},
        std::pair{GLFW_KEY_F10, Key::F10}, std::pair{GLFW_KEY_F11, Key::F11},
        std::pair{GLFW_KEY_F12, Key::F12}, std::pair{GLFW_KEY_F13, Key::F13},
        std::pair{GLFW_KEY_F14, Key::F14}, std::pair{GLFW_KEY_F15, Key::F15},
        std::pair{GLFW_KEY_F16, Key::F16}, std::pair{GLFW_KEY_F17, Key::F17},
        std::pair{GLFW_KEY_F18, Key::F18}, std::pair{GLFW_KEY_F19, Key::F19},
        std::pair{GLFW_KEY_F20, Key::F20}, std::pair{GLFW_KEY_F21, Key::F21},
        std::pair{GLFW_KEY_F22, Key::F22}, std::pair{GLFW_KEY_F23, Key::F23},
        std::pair{GLFW_KEY_F24, Key::F24}, std::pair{GLFW_KEY_F25, Key::F25},
        std::pair{GLFW_KEY_KP_0, Key::Keypad0}, std::pair{GLFW_KEY_KP_1, Key::Keypad1},
        std::pair{GLFW_KEY_KP_2, Key::Keypad2}, std::pair{GLFW_KEY_KP_3, Key::Keypad3},
        std::pair{GLFW_KEY_KP_4, Key::Keypad4}, std::pair{GLFW_KEY_KP_5, Key::Keypad5},
        std::pair{GLFW_KEY_KP_6, Key::Keypad6}, std::pair{GLFW_KEY_KP_7, Key::Keypad7},
        std::pair{GLFW_KEY_KP_8, Key::Keypad8}, std::pair{GLFW_KEY_KP_9, Key::Keypad9},
        std::pair{GLFW_KEY_KP_DECIMAL, Key::KeypadDecimal}, std::pair{GLFW_KEY_KP_DIVIDE, Key::KeypadDivide},
        std::pair{GLFW_KEY_KP_MULTIPLY, Key::KeypadMultiply}, std::pair{GLFW_KEY_KP_SUBTRACT, Key::KeypadSubtract},
        std::pair{GLFW_KEY_KP_ADD, Key::KeypadAdd}, std::pair{GLFW_KEY_KP_ENTER, Key::KeypadEnter},
        std::pair{GLFW_KEY_KP_EQUAL, Key::KeypadEqual}, std::pair{GLFW_KEY_LEFT_SHIFT, Key::LeftShift},
        std::pair{GLFW_KEY_LEFT_CONTROL, Key::LeftControl}, std::pair{GLFW_KEY_LEFT_ALT, Key::LeftAlt},
        std::pair{GLFW_KEY_LEFT_SUPER, Key::LeftSuper}, std::pair{GLFW_KEY_RIGHT_SHIFT, Key::RightShift},
        std::pair{GLFW_KEY_RIGHT_CONTROL, Key::RightControl}, std::pair{GLFW_KEY_RIGHT_ALT, Key::RightAlt},
        std::pair{GLFW_KEY_RIGHT_SUPER, Key::RightSuper}, std::pair{GLFW_KEY_MENU, Key::Menu},
    };
    for (const auto& [native, key] : entries) map[native] = key;
    return map;
}();

constexpr std::array mouseMap = {
    Key::MouseLeft, Key::MouseRight, Key::MouseMiddle, Key::Mouse4,
    Key::Mouse5, Key::Mouse6, Key::Mouse7, Key::Mouse8,
};
constexpr std::array actionMap = {Action::Release, Action::Press};
static_assert(static_cast<unsigned int>(Modifier::Shift) == GLFW_MOD_SHIFT);
static_assert(static_cast<unsigned int>(Modifier::Control) == GLFW_MOD_CONTROL);
static_assert(static_cast<unsigned int>(Modifier::Alt) == GLFW_MOD_ALT);
static_assert(static_cast<unsigned int>(Modifier::Super) == GLFW_MOD_SUPER);
static_assert(static_cast<unsigned int>(Modifier::CapsLock) == GLFW_MOD_CAPS_LOCK);
static_assert(static_cast<unsigned int>(Modifier::NumLock) == GLFW_MOD_NUM_LOCK);

}

InputManager::~InputManager() { DCHECK(!window); }

void InputManager::init()
{
    if (window != nullptr) return;
    DCHECK(context().window && context().window->nativeHandle() &&
        context().config && context().inputManager == this,
        "InputManager requires an initialized Window and ConfigManager in Context");

    bindings = context().config->commandConfig().bindings;

    window = context().window;
    window->setKeyCallback([this](int key, int, int action, int mods) { handleKey(key, action, mods); });
    window->setMouseButtonCallback([this](int button, int action, int mods) { handleMouseButton(button, action, mods); });
    window->setFocusCallback([this](int focused) { handleFocus(focused); });
}

void InputManager::shutdown() noexcept
{
    // Shut down the ImGui backend before this manager, and the Window afterwards.
    if (window != nullptr && window->nativeHandle() != nullptr)
    {
        window->setKeyCallback(nullptr);
        window->setMouseButtonCallback(nullptr);
        window->setFocusCallback(nullptr);
    }

    window = nullptr;
    bindings.clear();
    inputEvents.clear();
    inputSet.fill(Action::Release);
    commandSet.fill(false);
}

void InputManager::tick()
{
    // Num Lock and Caps Lock never participate in shortcut matching.
    constexpr Modifier shortcutModifiers =
        Modifier::Shift | Modifier::Control | Modifier::Alt | Modifier::Super;

    commandSet.fill(false);
    for (const auto& event : inputEvents)
    {
        if (event.focusLost)
        {
            inputSet.fill(Action::Release);
            commandSet.fill(false);
            continue;
        }

        const auto index = inputIndex(event.key);
        if (inputSet[index] == event.action) continue;
        inputSet[index] = event.action;

        for (const auto& binding : bindings)
        {
            if (binding.key == event.key &&
                (binding.modifiers & shortcutModifiers) == (event.mods & shortcutModifiers) &&
                binding.action == event.action)
                commandSet[static_cast<size_t>(binding.command)] = true;
        }
    }

    // 最后处理hold命令
    for (const auto& binding : bindings)
    {
        if (binding.action == Action::Hold && inputSet[inputIndex(binding.key)] == Action::Press)
            commandSet[static_cast<size_t>(binding.command)] = true;
    }

    inputEvents.clear();
}

bool InputManager::get(Command command) const
{
    return commandSet[static_cast<size_t>(command)];
}

void InputManager::handleKey(int key, int action, int mods)
{
    if (key >= 0 && key < static_cast<int>(keyMap.size()) && keyMap[key] != Key::Unknown &&
        action >= 0 && action < static_cast<int>(actionMap.size()))
        inputEvents.push_back({keyMap[key], actionMap[action], static_cast<Modifier>(mods)});
}

void InputManager::handleMouseButton(int button, int action, int mods)
{
    if (button >= 0 && button < static_cast<int>(mouseMap.size()) &&
        action >= 0 && action < static_cast<int>(actionMap.size()))
        inputEvents.push_back({mouseMap[button], actionMap[action], static_cast<Modifier>(mods)});
}

void InputManager::handleFocus(int focused)
{
    if (!focused)
    {
        inputEvents.push_back({.focusLost = true});
    }
}
