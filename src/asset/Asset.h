#pragma once

#include "asset/AssetID.h"

#include <concepts>
#include <string_view>
#include <tuple>

struct Asset
{
};

template <class T>
concept AssetType = std::derived_from<T, Asset> && requires
{
    typename T::Desc;
    typename T::ID;
    requires std::same_as<typename T::ID, AssetID<T>>;
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

#define DECLARE_ASSET(Name) \
    struct Name : Asset \
    { \
        struct Desc; \
        using ID = AssetID<Name>; \
        static constexpr std::string_view dir = #Name; \
    };

#define ASSET_FOR_EACH_1(M, A) M(A)
#define ASSET_FOR_EACH_2(M, A, ...) M(A) ASSET_FOR_EACH_1(M, __VA_ARGS__)
#define ASSET_FOR_EACH_3(M, A, ...) M(A) ASSET_FOR_EACH_2(M, __VA_ARGS__)
#define ASSET_FOR_EACH_4(M, A, ...) M(A) ASSET_FOR_EACH_3(M, __VA_ARGS__)
#define ASSET_FOR_EACH_5(M, A, ...) M(A) ASSET_FOR_EACH_4(M, __VA_ARGS__)
#define ASSET_FOR_EACH_6(M, A, ...) M(A) ASSET_FOR_EACH_5(M, __VA_ARGS__)
#define ASSET_FOR_EACH_7(M, A, ...) M(A) ASSET_FOR_EACH_6(M, __VA_ARGS__)
#define ASSET_FOR_EACH_8(M, A, ...) M(A) ASSET_FOR_EACH_7(M, __VA_ARGS__)
#define ASSET_FOR_EACH_9(M, A, ...) M(A) ASSET_FOR_EACH_8(M, __VA_ARGS__)
#define ASSET_FOR_EACH_10(M, A, ...) M(A) ASSET_FOR_EACH_9(M, __VA_ARGS__)
#define ASSET_FOR_EACH_11(M, A, ...) M(A) ASSET_FOR_EACH_10(M, __VA_ARGS__)
#define ASSET_FOR_EACH_12(M, A, ...) M(A) ASSET_FOR_EACH_11(M, __VA_ARGS__)
#define ASSET_FOR_EACH_13(M, A, ...) M(A) ASSET_FOR_EACH_12(M, __VA_ARGS__)
#define ASSET_FOR_EACH_14(M, A, ...) M(A) ASSET_FOR_EACH_13(M, __VA_ARGS__)
#define ASSET_FOR_EACH_15(M, A, ...) M(A) ASSET_FOR_EACH_14(M, __VA_ARGS__)
#define ASSET_FOR_EACH_16(M, A, ...) M(A) ASSET_FOR_EACH_15(M, __VA_ARGS__)
#define ASSET_FOR_EACH_SELECT( \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, Name, ...) Name
#define ASSET_FOR_EACH(M, ...) \
    ASSET_FOR_EACH_SELECT(__VA_ARGS__, \
        ASSET_FOR_EACH_16, ASSET_FOR_EACH_15, ASSET_FOR_EACH_14, ASSET_FOR_EACH_13, \
        ASSET_FOR_EACH_12, ASSET_FOR_EACH_11, ASSET_FOR_EACH_10, ASSET_FOR_EACH_9, \
        ASSET_FOR_EACH_8, ASSET_FOR_EACH_7, ASSET_FOR_EACH_6, ASSET_FOR_EACH_5, \
        ASSET_FOR_EACH_4, ASSET_FOR_EACH_3, ASSET_FOR_EACH_2, ASSET_FOR_EACH_1)(M, __VA_ARGS__)

#define REGISTER_ASSETS(...) \
    ASSET_FOR_EACH(DECLARE_ASSET, __VA_ARGS__) \
    using AssetTypes = AssetTypeList<__VA_ARGS__>;
