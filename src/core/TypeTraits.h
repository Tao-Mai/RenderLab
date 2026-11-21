#pragma once

#include <map>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/fwd.hpp>

template <class T>
inline constexpr bool IsVector = false;

template <class T, class Allocator>
inline constexpr bool IsVector<std::vector<T, Allocator>> = true;

template <class T>
inline constexpr bool IsOptional = false;

template <class T>
inline constexpr bool IsVariant = false;

template <class... T>
inline constexpr bool IsVariant<std::variant<T...>> = true;

template <class T>
inline constexpr bool IsOptional<std::optional<T>> = true;

template <class T>
inline constexpr bool IsMap = false;

template <class Key, class Value, class Compare, class Allocator>
inline constexpr bool IsMap<std::map<Key, Value, Compare, Allocator>> = true;

template <class Key, class Value, class Hash, class Equal, class Allocator>
inline constexpr bool IsMap<std::unordered_map<Key, Value, Hash, Equal, Allocator>> = true;

template <class T>
inline constexpr bool IsGlmVector = false;

template <glm::length_t N, class T, glm::qualifier Q>
inline constexpr bool IsGlmVector<glm::vec<N, T, Q>> = true;

template <class T>
inline constexpr bool kIsGlmMatrix = false;
template <glm::length_t C, glm::length_t R, class T, glm::qualifier Q>
inline constexpr bool kIsGlmMatrix<glm::mat<C, R, T, Q>> = true;
