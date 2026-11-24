#pragma once

#include "core/Serializer.h"

#include <algorithm>

namespace metadata
{
template <class T, auto Member>
constexpr Property bindField(Field<Member> field, std::optional<SliderRange> range = {})
{
    using Value = std::remove_cvref_t<decltype(std::declval<T&>().*Member)>;
    return {
        .name = field.name,
        .type = typeid(Value),
        .range = range,
        .isEnum = std::is_enum_v<Value>,
        .address = [](void* object) -> void* { return std::addressof(static_cast<T*>(object)->*Member); },
        .read = [](const void* object) { return Serialize(static_cast<const T*>(object)->*Member); },
        .write = [](void* object, const json& value) { Deserialize(value, static_cast<T*>(object)->*Member); },
    };
}

template <class T, auto Member>
constexpr Property bindField(SliderField<Member> field)
{
    return bindField<T>(static_cast<Field<Member>>(field), field.range);
}

template <class T>
inline constexpr auto kProperties = std::apply(
    [](const auto&... field) { return std::array<Property, sizeof...(field)>{bindField<T>(field)...}; },
    Schema<T>::kFields
);

template <class T>
constexpr Entry<Record> bindRecord()
{
    const char* id = "";
    if constexpr (requires { Schema<T>::kTypeId; })
        id = Schema<T>::kTypeId;

    return {typeid(T), {id, Schema<T>::kAllowUnknownFields, kProperties<T>}};
}

template <class T>
inline constexpr auto kEnumNames = []
{
    std::array<const char*, Enum<T>::kValues.size()> names{};
    for (size_t index = 0; index < names.size(); ++index)
        names[index] = Enum<T>::kValues[index].name;
    return names;
}();

template <class T>
constexpr Entry<EnumInfo> bindEnum()
{
    return {typeid(T), {
        .flags = Enum<T>::kFlags,
        .names = kEnumNames<T>,
        .values = Enum<T>::kValues.data(),
        .index = [](const void* value) -> std::size_t
        {
            return std::ranges::find(Enum<T>::kValues, *static_cast<const T*>(value), &EnumValue<T>::value) -
                   Enum<T>::kValues.begin();
        },
        .select = [](void* value, std::size_t index) { *static_cast<T*>(value) = Enum<T>::kValues[index].value; },
    }};
}

template <class T>
constexpr Entry<std::span<const LayoutField>> bindLayout()
{
    return {typeid(T), Layout<T>::kFields};
}

template <class T, class Base>
void addPolymorphicType(TypeRegistry<Base>& registry)
{
    static_assert(std::derived_from<T, Base>);
    static_assert(std::has_virtual_destructor_v<Base>);
    static_assert(!std::is_abstract_v<T>);

    constexpr auto kId = Schema<T>::kTypeId;
    using Parent = typename Schema<T>::Parent;
    constexpr std::string_view kParent = []() -> std::string_view
    {
        if constexpr (std::is_void_v<Parent>)
            return {};
        else
            return Schema<Parent>::kTypeId;
    }();

    const auto entry = registry.entries.emplace(
        kId,
        typename TypeRegistry<Base>::Entry{
            .id = kId,
            .parent = kParent,
            .create = []() -> std::unique_ptr<Base> { return std::make_unique<T>(); },
            .serialize = [](const Base& value) { return Serialize(static_cast<const T&>(value)); },
            .deserialize = [](const json& data) -> std::unique_ptr<Base>
            {
                auto result = std::make_unique<T>();
                Deserialize(data, *result);
                return result;
            },
        }
    ).first;
    registry.nativeTypes.emplace(typeid(T), &entry->second);
}
}
