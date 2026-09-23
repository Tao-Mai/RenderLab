#include "platform/input_method.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

InputMethod::~InputMethod()
{
    restore();
}

void InputMethod::activateEnglish() noexcept
{
#ifdef _WIN32
    if (active)
    {
        return;
    }

    // Save layout name before switching. On Win8+, LoadKeyboardLayout+KLF_ACTIVATE
    // changes the system input language; restore must use the same API.
    static_assert(sizeof(previousLayoutName) / sizeof(previousLayoutName[0]) >= KL_NAMELENGTH);
    if (GetKeyboardLayoutNameW(previousLayoutName) == 0)
    {
        return;
    }
    previousLayout = GetKeyboardLayout(0);

    if (LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE) != nullptr)
    {
        active = true;
    }
#endif
}

void InputMethod::restore() noexcept
{
#ifdef _WIN32
    if (active)
    {
        // ActivateKeyboardLayout alone only affects this process and cannot undo the
        // system-wide change made by LoadKeyboardLayout+KLF_ACTIVATE on Win8+.
        LoadKeyboardLayoutW(previousLayoutName, KLF_ACTIVATE);
        if (previousLayout != nullptr)
        {
            ActivateKeyboardLayout(static_cast<HKL>(previousLayout), 0);
        }
    }
    previousLayoutName[0] = L'\0';
#endif
    previousLayout = nullptr;
    active = false;
}
