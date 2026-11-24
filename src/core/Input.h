#pragma once

#include "core/MetadataAnnotations.h"

enum class RL_ENUM Key : int
{
    Unknown RL_ENUM_VALUE("Unknown") = -1,

    Space RL_ENUM_VALUE("Space"),
    Apostrophe RL_ENUM_VALUE("Apostrophe"),
    Comma RL_ENUM_VALUE("Comma"),
    Minus RL_ENUM_VALUE("Minus"),
    Period RL_ENUM_VALUE("Period"),
    Slash RL_ENUM_VALUE("Slash"),
    Semicolon RL_ENUM_VALUE("Semicolon"),
    Equal RL_ENUM_VALUE("Equal"),
    LeftBracket RL_ENUM_VALUE("LeftBracket"),
    Backslash RL_ENUM_VALUE("Backslash"),
    RightBracket RL_ENUM_VALUE("RightBracket"),
    GraveAccent RL_ENUM_VALUE("GraveAccent"),

    Digit0 RL_ENUM_VALUE("Digit0"),
    Digit1 RL_ENUM_VALUE("Digit1"),
    Digit2 RL_ENUM_VALUE("Digit2"),
    Digit3 RL_ENUM_VALUE("Digit3"),
    Digit4 RL_ENUM_VALUE("Digit4"),
    Digit5 RL_ENUM_VALUE("Digit5"),
    Digit6 RL_ENUM_VALUE("Digit6"),
    Digit7 RL_ENUM_VALUE("Digit7"),
    Digit8 RL_ENUM_VALUE("Digit8"),
    Digit9 RL_ENUM_VALUE("Digit9"),

    A RL_ENUM_VALUE("A"),
    B RL_ENUM_VALUE("B"),
    C RL_ENUM_VALUE("C"),
    D RL_ENUM_VALUE("D"),
    E RL_ENUM_VALUE("E"),
    F RL_ENUM_VALUE("F"),
    G RL_ENUM_VALUE("G"),
    H RL_ENUM_VALUE("H"),
    I RL_ENUM_VALUE("I"),
    J RL_ENUM_VALUE("J"),
    K RL_ENUM_VALUE("K"),
    L RL_ENUM_VALUE("L"),
    M RL_ENUM_VALUE("M"),
    N RL_ENUM_VALUE("N"),
    O RL_ENUM_VALUE("O"),
    P RL_ENUM_VALUE("P"),
    Q RL_ENUM_VALUE("Q"),
    R RL_ENUM_VALUE("R"),
    S RL_ENUM_VALUE("S"),
    T RL_ENUM_VALUE("T"),
    U RL_ENUM_VALUE("U"),
    V RL_ENUM_VALUE("V"),
    W RL_ENUM_VALUE("W"),
    X RL_ENUM_VALUE("X"),
    Y RL_ENUM_VALUE("Y"),
    Z RL_ENUM_VALUE("Z"),

    World1 RL_ENUM_VALUE("World1"),
    World2 RL_ENUM_VALUE("World2"),

    Escape RL_ENUM_VALUE("Escape"),
    Enter RL_ENUM_VALUE("Enter"),
    Tab RL_ENUM_VALUE("Tab"),
    Backspace RL_ENUM_VALUE("Backspace"),
    Insert RL_ENUM_VALUE("Insert"),
    Delete RL_ENUM_VALUE("Delete"),
    Right RL_ENUM_VALUE("Right"),
    Left RL_ENUM_VALUE("Left"),
    Down RL_ENUM_VALUE("Down"),
    Up RL_ENUM_VALUE("Up"),
    PageUp RL_ENUM_VALUE("PageUp"),
    PageDown RL_ENUM_VALUE("PageDown"),
    Home RL_ENUM_VALUE("Home"),
    End RL_ENUM_VALUE("End"),

    CapsLock RL_ENUM_VALUE("CapsLock"),
    ScrollLock RL_ENUM_VALUE("ScrollLock"),
    NumLock RL_ENUM_VALUE("NumLock"),
    PrintScreen RL_ENUM_VALUE("PrintScreen"),
    Pause RL_ENUM_VALUE("Pause"),

    F1 RL_ENUM_VALUE("F1"),
    F2 RL_ENUM_VALUE("F2"),
    F3 RL_ENUM_VALUE("F3"),
    F4 RL_ENUM_VALUE("F4"),
    F5 RL_ENUM_VALUE("F5"),
    F6 RL_ENUM_VALUE("F6"),
    F7 RL_ENUM_VALUE("F7"),
    F8 RL_ENUM_VALUE("F8"),
    F9 RL_ENUM_VALUE("F9"),
    F10 RL_ENUM_VALUE("F10"),
    F11 RL_ENUM_VALUE("F11"),
    F12 RL_ENUM_VALUE("F12"),
    F13 RL_ENUM_VALUE("F13"),
    F14 RL_ENUM_VALUE("F14"),
    F15 RL_ENUM_VALUE("F15"),
    F16 RL_ENUM_VALUE("F16"),
    F17 RL_ENUM_VALUE("F17"),
    F18 RL_ENUM_VALUE("F18"),
    F19 RL_ENUM_VALUE("F19"),
    F20 RL_ENUM_VALUE("F20"),
    F21 RL_ENUM_VALUE("F21"),
    F22 RL_ENUM_VALUE("F22"),
    F23 RL_ENUM_VALUE("F23"),
    F24 RL_ENUM_VALUE("F24"),
    F25 RL_ENUM_VALUE("F25"),

    Keypad0 RL_ENUM_VALUE("Keypad0"),
    Keypad1 RL_ENUM_VALUE("Keypad1"),
    Keypad2 RL_ENUM_VALUE("Keypad2"),
    Keypad3 RL_ENUM_VALUE("Keypad3"),
    Keypad4 RL_ENUM_VALUE("Keypad4"),
    Keypad5 RL_ENUM_VALUE("Keypad5"),
    Keypad6 RL_ENUM_VALUE("Keypad6"),
    Keypad7 RL_ENUM_VALUE("Keypad7"),
    Keypad8 RL_ENUM_VALUE("Keypad8"),
    Keypad9 RL_ENUM_VALUE("Keypad9"),
    KeypadDecimal RL_ENUM_VALUE("KeypadDecimal"),
    KeypadDivide RL_ENUM_VALUE("KeypadDivide"),
    KeypadMultiply RL_ENUM_VALUE("KeypadMultiply"),
    KeypadSubtract RL_ENUM_VALUE("KeypadSubtract"),
    KeypadAdd RL_ENUM_VALUE("KeypadAdd"),
    KeypadEnter RL_ENUM_VALUE("KeypadEnter"),
    KeypadEqual RL_ENUM_VALUE("KeypadEqual"),

    LeftShift RL_ENUM_VALUE("LeftShift"),
    LeftControl RL_ENUM_VALUE("LeftControl"),
    LeftAlt RL_ENUM_VALUE("LeftAlt"),
    LeftSuper RL_ENUM_VALUE("LeftSuper"),
    RightShift RL_ENUM_VALUE("RightShift"),
    RightControl RL_ENUM_VALUE("RightControl"),
    RightAlt RL_ENUM_VALUE("RightAlt"),
    RightSuper RL_ENUM_VALUE("RightSuper"),
    Menu RL_ENUM_VALUE("Menu"),

    MouseLeft RL_ENUM_VALUE("MouseLeft"),
    MouseRight RL_ENUM_VALUE("MouseRight"),
    MouseMiddle RL_ENUM_VALUE("MouseMiddle"),
    Mouse4 RL_ENUM_VALUE("Mouse4"),
    Mouse5 RL_ENUM_VALUE("Mouse5"),
    Mouse6 RL_ENUM_VALUE("Mouse6"),
    Mouse7 RL_ENUM_VALUE("Mouse7"),
    Mouse8 RL_ENUM_VALUE("Mouse8"),
};

enum class RL_ENUM Action : int
{
    Release RL_ENUM_VALUE("Release"),
    Press RL_ENUM_VALUE("Press"),
    Hold RL_ENUM_VALUE("Hold"),
};

enum class RL_FLAGS Modifier : unsigned int
{
    None RL_ENUM_VALUE("None") = 0,
    Shift RL_ENUM_VALUE("Shift") = 1 << 0,
    Control RL_ENUM_VALUE("Control") = 1 << 1,
    Alt RL_ENUM_VALUE("Alt") = 1 << 2,
    Super RL_ENUM_VALUE("Super") = 1 << 3,
    CapsLock RL_ENUM_VALUE("CapsLock") = 1 << 4,
    NumLock RL_ENUM_VALUE("NumLock") = 1 << 5,
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
enum class RL_ENUM Command
{
    Quit RL_ENUM_VALUE("Quit"),
    ToggleFreeMovement RL_ENUM_VALUE("ToggleFreeMovement"),
    Navigate RL_ENUM_VALUE("Navigate"),
    MoveForward RL_ENUM_VALUE("MoveForward"),
    MoveBackward RL_ENUM_VALUE("MoveBackward"),
    MoveLeft RL_ENUM_VALUE("MoveLeft"),
    MoveRight RL_ENUM_VALUE("MoveRight"),
    MoveUp RL_ENUM_VALUE("MoveUp"),
    MoveDown RL_ENUM_VALUE("MoveDown"),
    Sprint RL_ENUM_VALUE("Sprint"),
    SaveScene RL_ENUM_VALUE("SaveScene"),
    SelectObject RL_ENUM_VALUE("SelectObject"),
    GizmoTranslate RL_ENUM_VALUE("GizmoTranslate"),
    GizmoRotate RL_ENUM_VALUE("GizmoRotate"),
    GizmoScale RL_ENUM_VALUE("GizmoScale"),
};

// A binding uses one keyboard key or mouse button, with optional modifiers for Press / Release.
struct RL_RECORD CommandBinding
{
    RL_FIELD("command") Command command;
    RL_FIELD("key") Key key;
    RL_FIELD("modifiers") Modifier modifiers;
    RL_FIELD("action") Action action;
};

#include RL_METADATA_HEADER("metadata/src/core/Input.generated.h")
