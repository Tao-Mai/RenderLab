#include "platform/input_method.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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

    previousLayout = GetKeyboardLayout(0);
    const HKL englishLayout = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE);
    if (englishLayout != nullptr)
    {
        ActivateKeyboardLayout(englishLayout, 0);
        active = true;
    }
#endif
}

void InputMethod::restore() noexcept
{
#ifdef _WIN32
    if (active && previousLayout != nullptr)
    {
        ActivateKeyboardLayout(static_cast<HKL>(previousLayout), 0);
    }
#endif
    previousLayout = nullptr;
    active = false;
}
