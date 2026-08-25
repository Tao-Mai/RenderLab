#pragma once

#include <entt/meta/factory.hpp>

#define META_CAT_(A, B) A##B
#define META_CAT(A, B)  META_CAT_(A, B)

#define META_MEMBER(Type, field) .data<&Type::field>(#field##_hs)

#define META_MEMBERS_1(Type, f1) META_MEMBER(Type, f1)
#define META_MEMBERS_2(Type, f1, f2) META_MEMBERS_1(Type, f1) META_MEMBERS_1(Type, f2)
#define META_MEMBERS_3(Type, f1, f2, f3) META_MEMBERS_1(Type, f1) META_MEMBERS_2(Type, f2, f3)
#define META_MEMBERS_4(Type, f1, f2, f3, f4) META_MEMBERS_1(Type, f1) META_MEMBERS_3(Type, f2, f3, f4)
#define META_MEMBERS_5(Type, f1, f2, f3, f4, f5) META_MEMBERS_1(Type, f1) META_MEMBERS_4(Type, f2, f3, f4, f5)
#define META_MEMBERS_6(Type, f1, f2, f3, f4, f5, f6) META_MEMBERS_1(Type, f1) META_MEMBERS_5(Type, f2, f3, f4, f5, f6)
#define META_MEMBERS_7(Type, f1, f2, f3, f4, f5, f6, f7) META_MEMBERS_1(Type, f1) META_MEMBERS_6(Type, f2, f3, f4, f5, f6, f7)
#define META_MEMBERS_8(Type, f1, f2, f3, f4, f5, f6, f7, f8) META_MEMBERS_1(Type, f1) META_MEMBERS_7(Type, f2, f3, f4, f5, f6, f7, f8)

#define META_GET_N(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define META_NARG(...) META_GET_N(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)
#define META_PASTE_(a, b) a##b
#define META_PASTE(a, b) META_PASTE_(a, b)
#define META_MEMBERS(Type, ...) META_PASTE(META_MEMBERS_, META_NARG(__VA_ARGS__))(Type, __VA_ARGS__)

// Name：entt 类型名（#Name）；后续参数为成员字段名。
// 示例：META_REGISTER(Light, Light, position, color, intensity, constant, linear, quadratic)
#define META_REGISTER(Type, Name, ...)                                                            \
    inline const bool META_CAT(meta_bind_, Name) = ([]() -> bool {                                \
        using entt::literals::operator""_hs;                                                      \
        static_cast<void>(entt::meta_factory<Type>().type(#Name) META_MEMBERS(Type, __VA_ARGS__)); \
        return true;                                                                              \
    }())
