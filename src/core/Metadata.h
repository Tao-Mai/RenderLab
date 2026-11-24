#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeinfo>

#include <nlohmann/json_fwd.hpp>

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

struct Property
{
    const char* name;
    const std::type_info& type;
    std::optional<SliderRange> range;
    bool isEnum;
    void* (*address)(void* object);
    nlohmann::json (*read)(const void* object);
    void (*write)(void* object, const nlohmann::json& value);
};

struct Record
{
    const char* id;
    bool allowUnknownFields;
    std::span<const Property> fields;
};

struct EnumInfo
{
    bool flags;
    std::span<const char* const> names;
    const void* values;
    std::size_t (*index)(const void* value);
    void (*select)(void* value, std::size_t index);
};

template <class T>
struct Entry
{
    const std::type_info& type;
    T data;
};

struct Module
{
    std::span<const Entry<Record>> records;
    std::span<const Entry<EnumInfo>> enums;
    std::span<const Entry<std::span<const LayoutField>>> layouts;
};

// The generated module owns the descriptors and the functions they point to.
class ModuleRegistration
{
public:
    explicit ModuleRegistration(Module module);
    ~ModuleRegistration();

    ModuleRegistration(const ModuleRegistration&) = delete;
    ModuleRegistration& operator=(const ModuleRegistration&) = delete;

private:
    Module module;
};

[[nodiscard]] const Record& record(const std::type_info& type);
[[nodiscard]] const EnumInfo& enumInfo(const std::type_info& type);
[[nodiscard]] std::span<const LayoutField> layoutFields(const std::type_info& type);

template <class T>
[[nodiscard]] std::span<const EnumValue<T>> enumValues(const EnumInfo& info)
{
    return {static_cast<const EnumValue<T>*>(info.values), info.names.size()};
}

template <class... Types, class Visit>
void visitTypes(TypeList<Types...>, Visit&& visit)
{
    (visit.template operator()<Types>(record(typeid(Types)).id), ...);
}

template <class Base, class Visit>
constexpr void visitRegisteredTypes(Visit&& visit)
{
    [&]<class... Types>(TypeList<Types...>)
    {
        (visit.template operator()<Types>(Schema<Types>::kTypeId), ...);
    }(typename RegisteredTypes<Base>::Types{});
}

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
