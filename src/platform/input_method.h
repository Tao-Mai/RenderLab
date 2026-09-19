#pragma once

class InputMethod
{
  public:
    InputMethod() = default;
    ~InputMethod();

    InputMethod(const InputMethod &) = delete;
    InputMethod &operator=(const InputMethod &) = delete;

    void activateEnglish() noexcept;
    void restore() noexcept;

  private:
    void *previousLayout = nullptr;
    bool active = false;
};
