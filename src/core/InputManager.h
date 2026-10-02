#pragma once

#include "core/Input.h"

#include <array>
#include <vector>

class Window;

class InputManager
{
public:
    InputManager() = default;
    ~InputManager();
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    void init();
    void shutdown() noexcept;
    // Process queued Press / Release events, then generate Hold commands from Press states.
    void tick();
    [[nodiscard]] bool get(Command command) const;

private:
    static constexpr size_t commandCount = static_cast<size_t>(Command::GizmoScale) + 1;
    static constexpr size_t inputCount = static_cast<size_t>(Key::Mouse8) + 1;

    struct InputEvent
    {
        Key key = Key::Unknown;
        Action action = Action::Release;
        Modifier mods = Modifier::None;
        bool focusLost = false;
    };

    Window* window = nullptr;
    std::vector<CommandBinding> bindings;
    std::vector<InputEvent> inputEvents;
    // Latest input action: Press means currently down; Release means up.
    std::array<Action, inputCount> inputSet{};
    std::array<bool, commandCount> commandSet{};

    static constexpr size_t inputIndex(Key key) { return static_cast<size_t>(key); }
    void handleKey(int key, int action, int mods);
    void handleMouseButton(int button, int action, int mods);
    void handleFocus(int focused);
};
