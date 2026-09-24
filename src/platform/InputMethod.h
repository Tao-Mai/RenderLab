#pragma once

class InputMethod
{
  public:
    InputMethod() = default;
    ~InputMethod();

    InputMethod(const InputMethod&) = delete;
    InputMethod& operator=(const InputMethod&) = delete;

    void activateEnglish() noexcept;
    void restore() noexcept;

  private:
#ifdef _WIN32
    // KL_NAMELENGTH == 9
    wchar_t previousLayoutName[9]{};
#endif
    void* previousLayout = nullptr;
    bool active = false;
};
