#pragma once

#include "asset/AssetID.h"

#include <concepts>
#include <string_view>
#include <tuple>


struct Asset
{
    bool operator==(const Asset&) const = default;
};

template <class T>
concept AssetType = std::derived_from<T, Asset> && requires(T& value) {
    typename T::ID;
    requires std::same_as<typename T::ID, AssetID<T>>;
    { value.id } -> std::same_as<AssetID<T>&>;
    { T::dir } -> std::convertible_to<std::string_view>;
};

template <AssetType... Assets>
struct AssetTypeList
{
    template <class F>
    static constexpr void forEach(F&& function)
    {
        (function.template operator()<Assets>(), ...);
    }

    template <template <class> class Wrapper>
    using wrapTypes = std::tuple<Wrapper<Assets>...>;
};

enum class Source
{
    File,
    Builtin
};
