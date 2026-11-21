#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace Gui
{
class Widgets
{
public:
    class Group
    {
    public:
        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;
        ~Group();

        explicit operator bool() const { return open; }

    private:
        Group(const char* label, bool defaultOpen);

        bool open;
        friend class Widgets;
    };

    bool checkbox(const char* label, bool& value);
    bool slider(const char* label, float& value, float minimum, float maximum,
                const char* format = nullptr, bool clampInput = false);
    bool slider(const char* label, double& value, double minimum, double maximum);
    bool slider(const char* label, int32_t& value, int32_t minimum, int32_t maximum);
    bool slider(const char* label, uint32_t& value, uint32_t minimum, uint32_t maximum, bool clampInput = true);
    bool slider(const char* label, int64_t& value, int64_t minimum, int64_t maximum);
    bool input(const char* label, double& value);
    bool input(const char* label, int64_t& value);
    bool drag(const char* label, float& value, float speed, float minimum, float maximum, const char* format);
    bool dragRange(const char* label, float& low, float& high, float speed,
                   float minimum, float maximum, const char* format);

    bool dropdown(const char* label, std::span<const char* const> choices, std::size_t& selected,
                  const char* preview = nullptr);
    bool dropdown(const char* label, std::span<const std::string> choices, std::size_t& selected);

    // Labels follow the contiguous enum values starting at zero.
    template <class Enum>
        requires std::is_enum_v<Enum>
    bool dropdown(const char* label, Enum& value, std::initializer_list<const char*> choices)
    {
        auto selected = static_cast<std::size_t>(value);
        if (!dropdown(label, std::span(choices.begin(), choices.size()), selected))
            return false;

        value = static_cast<Enum>(selected);
        return true;
    }

    [[nodiscard]] Group group(const char* label, bool defaultOpen = false);
    bool resetHeader(const char* label);
    bool button(const char* label);
    bool colorEdit(const char* label, glm::vec3& color);
    void colorPreview(const char* label, const glm::vec3& color);
    void text(std::string_view text);
    void disabledText(const char* text);
    void tooltip(const char* text);
    void pushId(const char* id);
    void popId();
};
}
