#pragma once

#include "core/Serializer.h"
#include "editor/SettingsWidgets.h"
#include "render/graph/PassSettings.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <typeindex>
#include <type_traits>

class Renderer;
class VulkanContext;
struct EditorCameraConfig;

struct PassEditorContext
{
    const VulkanContext&      vulkan;
    const EditorCameraConfig& camera;
};

class PassEditorRegistry
{
public:
    template <class Pass, auto Draw = nullptr>
    void add(std::string label)
    {
        const bool added = editors.try_emplace(typeid(Pass), Editor{
            label,
            [](const void* object, const PassEditorContext& context) -> std::optional<nlohmann::json>
            {
                const auto& pass = *static_cast<const Pass*>(object);
                auto settings = pass.settings();
                bool changed;
                if constexpr (Draw != nullptr)
                    changed = Draw(settings, pass, context);
                else
                    changed = drawFields(settings);

                if (changed)
                    return Serialize(settings);
                return std::nullopt;
            },
        }).second;
        CHECK(added, "pass editor '{}' is already registered", label);
    }

    void draw(Renderer& renderer, const PassEditorContext& context) const;

private:
    struct Editor
    {
        std::string label;
        std::optional<nlohmann::json> (*draw)(const void*, const PassEditorContext&);
    };

    template <std::meta::info Member, class T>
    static bool drawField(const char* label, T& value)
    {
        if constexpr (std::is_same_v<T, bool>)
            return ImGui::Checkbox(label, &value);
        else if constexpr (std::is_enum_v<T>)
        {
            constexpr auto kOptions = []
            {
                static constexpr auto kEntries = std::define_static_array(std::meta::enumerators_of(^^T));
                std::array<T, kEntries.size()> values{};
                std::array<const char*, kEntries.size()> labels{};
                size_t index = 0;
                template for (constexpr auto kEntry : kEntries)
                {
                    values[index] = [:kEntry:];
                    labels[index] = std::define_static_string(std::meta::identifier_of(kEntry));
                    ++index;
                }
                return std::pair{values, labels};
            }();
            const auto& [values, labels] = kOptions;
            size_t selected = std::ranges::find(values, value) - values.begin();
            DCHECK(selected < values.size());
            if (!drawChoiceSetting(label, labels, selected))
                return false;

            value = values[selected];
            return true;
        }
        else
        {
            static_assert(std::is_same_v<T, float> || std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>,
                          "provide a custom editor for this settings type");
            constexpr auto kAnnotations =
                std::define_static_array(std::meta::annotations_of_with_type(Member, ^^PassSliderRange));
            static_assert(kAnnotations.size() == 1, "numeric settings require a PassSliderRange");
            constexpr auto kRange = std::meta::extract<PassSliderRange>(kAnnotations.front());
            constexpr auto kMin = static_cast<T>(kRange.minimum);
            constexpr auto kMax = static_cast<T>(kRange.maximum);
            return drawNumericSlider(label, value, kMin, kMax);
        }
    }

    template <class T>
    static bool drawFields(T& settings)
    {
        bool changed = false;
        constexpr auto kAccess = std::meta::access_context::current();
        template for (constexpr auto kMember : std::define_static_array(ReflectedDataMembers(^^T, kAccess)))
        {
            constexpr auto kName = std::define_static_string(std::meta::identifier_of(kMember));
            changed |= drawField<kMember>(kName, settings.[:kMember:]);
        }
        return changed;
    }

    std::map<std::type_index, Editor> editors;
};
