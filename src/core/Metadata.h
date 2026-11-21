#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace metadata
{
template <class T>
struct Schema;

template <class T>
struct Enum;

template <class Base>
struct RegisteredTypes;

template <class T>
struct Layout;

template <class... Types>
struct TypeList {};

template <class... Types, class Visit>
constexpr void visitTypes(TypeList<Types...>, Visit&& visit)
{
    (visit.template operator()<Types>(Schema<Types>::kTypeId), ...);
}

template <class Base, class Visit>
constexpr void visitRegisteredTypes(Visit&& visit)
{
    visitTypes(typename RegisteredTypes<Base>::Types{}, visit);
}

template <auto Member>
struct Field
{
    const char* name;

    template <class T>
    constexpr decltype(auto) get(T& object) const { return (object.*Member); }
};

struct SliderRange
{
    double minimum;
    double maximum;
};

template <auto Member>
struct SliderField : Field<Member>
{
    SliderRange range;
};

template <class T>
struct EnumValue
{
    T value;
    const char* name;
};

struct LayoutField
{
    const char* name;
    std::size_t offset;
    std::size_t size;
};

template <class T>
concept FlagEnum = std::is_enum_v<T> && Enum<T>::kFlags;

template <class T, class Visit>
constexpr void visitFields(T& object, Visit&& visit)
{
    std::apply([&](const auto&... fields) { (visit(fields, fields.get(object)), ...); },
               Schema<std::remove_cv_t<T>>::kFields);
}

template <class... Fields>
consteval bool uniqueFieldNames(const std::tuple<Fields...>& fields)
{
    const auto names = std::apply([](const auto&... field)
    {
        return std::array<std::string_view, sizeof...(Fields)>{field.name...};
    }, fields);

    for (std::size_t index = 1; index < names.size(); ++index)
        for (std::size_t previous = 0; previous < index; ++previous)
            if (names[index] == names[previous])
                return false;

    return true;
}
}
