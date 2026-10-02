#pragma once

#include "core/Annotations.h"

enum class Key : int
{
    Unknown = -1,

    Space, Apostrophe, Comma, Minus, Period, Slash,
    Semicolon, Equal, LeftBracket, Backslash, RightBracket, GraveAccent,

    Digit0, Digit1, Digit2, Digit3, Digit4,
    Digit5, Digit6, Digit7, Digit8, Digit9,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    World1, World2,

    Escape, Enter, Tab, Backspace, Insert, Delete,
    Right, Left, Down, Up, PageUp, PageDown, Home, End,

    CapsLock, ScrollLock, NumLock, PrintScreen, Pause,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10,
    F11, F12, F13, F14, F15, F16, F17, F18, F19, F20,
    F21, F22, F23, F24, F25,

    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4,
    Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
    KeypadDecimal, KeypadDivide, KeypadMultiply, KeypadSubtract,
    KeypadAdd, KeypadEnter, KeypadEqual,

    LeftShift, LeftControl, LeftAlt, LeftSuper,
    RightShift, RightControl, RightAlt, RightSuper,
    Menu,

    MouseLeft, MouseRight, MouseMiddle,
    Mouse4, Mouse5, Mouse6, Mouse7, Mouse8,
};

enum class Action : int
{
    Release, Press, Hold,
};

enum class [[=Flags{}]] Modifier : unsigned int
{
    None = 0,
    Shift = 1 << 0, Control = 1 << 1, Alt = 1 << 2, Super = 1 << 3,
    CapsLock = 1 << 4, NumLock = 1 << 5,
};

constexpr Modifier operator|(Modifier left, Modifier right)
{
    return static_cast<Modifier>(static_cast<unsigned int>(left) | static_cast<unsigned int>(right));
}

constexpr Modifier operator&(Modifier left, Modifier right)
{
    return static_cast<Modifier>(static_cast<unsigned int>(left) & static_cast<unsigned int>(right));
}

constexpr Modifier& operator|=(Modifier& left, Modifier right)
{
    return left = left | right;
}

// Commands are shared by all consumers; get() does not consume them.
enum class Command
{
    Quit, ToggleFreeMovement, Navigate,
    MoveForward, MoveBackward, MoveLeft, MoveRight, MoveUp, MoveDown, Sprint,
    SaveScene, SelectObject, GizmoTranslate, GizmoRotate, GizmoScale,
};

// A binding uses one keyboard key or mouse button, with optional modifiers for Press / Release.
struct CommandBinding
{
    Command command;
    Key key;
    Modifier modifiers;
    Action action;
};
