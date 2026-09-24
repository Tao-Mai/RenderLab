#pragma once

#include <entt/meta/factory.hpp>

#define REFLECT_EXPAND(...) __VA_ARGS__
#define REFLECT_CONCAT(a, b) REFLECT_CONCAT_I(a, b)
#define REFLECT_CONCAT_I(a, b) a##b

#define REFLECT_ARG_COUNT(...)                                                                 \
    REFLECT_EXPAND(REFLECT_ARG_COUNT_I(                                                        \
        __VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#define REFLECT_ARG_COUNT_I(                                                                   \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...)              \
    N

#define REFLECT_FIELD(Type, field) .template data<&Type::field>(#field)
#define REFLECT_ENUM_VALUE(EnumType, value) .data<EnumType::value>(#value)

#define REFLECT_FOREACH_1(m, t, a1) m(t, a1)
#define REFLECT_FOREACH_2(m, t, a1, a2) m(t, a1) m(t, a2)
#define REFLECT_FOREACH_3(m, t, a1, a2, a3) m(t, a1) m(t, a2) m(t, a3)
#define REFLECT_FOREACH_4(m, t, a1, a2, a3, a4)                                                \
    m(t, a1) m(t, a2) m(t, a3) m(t, a4)
#define REFLECT_FOREACH_5(m, t, a1, a2, a3, a4, a5)                                            \
    m(t, a1) m(t, a2) m(t, a3) m(t, a4) m(t, a5)
#define REFLECT_FOREACH_6(m, t, a1, a2, a3, a4, a5, a6)                                        \
    m(t, a1) m(t, a2) m(t, a3) m(t, a4) m(t, a5) m(t, a6)
#define REFLECT_FOREACH_7(m, t, a1, a2, a3, a4, a5, a6, a7)                                    \
    m(t, a1) m(t, a2) m(t, a3) m(t, a4) m(t, a5) m(t, a6) m(t, a7)
#define REFLECT_FOREACH_8(m, t, a1, a2, a3, a4, a5, a6, a7, a8)                                \
    m(t, a1) m(t, a2) m(t, a3) m(t, a4) m(t, a5) m(t, a6) m(t, a7) m(t, a8)
#define REFLECT_FOREACH_9(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9)                            \
    m(t, a1) m(t, a2) m(t, a3) m(t, a4) m(t, a5) m(t, a6) m(t, a7) m(t, a8) m(t, a9)
#define REFLECT_FOREACH_10(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)                      \
    m(t, a1) m(t, a2) m(t, a3) m(t, a4) m(t, a5) m(t, a6) m(t, a7) m(t, a8) m(t, a9) m(t, a10)
#define REFLECT_FOREACH_11(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11)                 \
    REFLECT_FOREACH_10(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) m(t, a11)
#define REFLECT_FOREACH_12(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12)            \
    REFLECT_FOREACH_11(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) m(t, a12)
#define REFLECT_FOREACH_13(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13)       \
    REFLECT_FOREACH_12(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) m(t, a13)
#define REFLECT_FOREACH_14(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14)  \
    REFLECT_FOREACH_13(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) m(t, a14)
#define REFLECT_FOREACH_15(                                                                    \
    m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15)                    \
    REFLECT_FOREACH_14(m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14)       \
    m(t, a15)
#define REFLECT_FOREACH_16(                                                                    \
    m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16)               \
    REFLECT_FOREACH_15(                                                                        \
        m, t, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15)                 \
    m(t, a16)

#define REFLECT_INVOKE(macro, ...) REFLECT_EXPAND(macro(__VA_ARGS__))

#define REFLECT_FOREACH(macro, Type, ...)                                                      \
    REFLECT_INVOKE(                                                                            \
        REFLECT_CONCAT(REFLECT_FOREACH_, REFLECT_ARG_COUNT(__VA_ARGS__)),                      \
        macro,                                                                                 \
        Type,                                                                                  \
        __VA_ARGS__)

// Register EnTT meta type + data members. Safe in headers via inline init-once variable.
#define REFLECT(Type, ...)                                                                     \
    inline const bool reflect_##Type = []() noexcept {                                         \
        (void)(entt::meta_factory<Type>{}                                                      \
                   .type(#Type) REFLECT_FOREACH(REFLECT_FIELD, Type, __VA_ARGS__));            \
        return true;                                                                           \
    }()

// Register EnTT meta enum constants (serialized by enumerator name).
#define REFLECT_ENUM(EnumType, Name, ...)                                                      \
    inline const bool reflect_enum_##Name = []() noexcept {                                    \
        (void)(entt::meta_factory<EnumType>{}                                                  \
                   .type(#Name) REFLECT_FOREACH(REFLECT_ENUM_VALUE, EnumType, __VA_ARGS__));   \
        return true;                                                                           \
    }()
