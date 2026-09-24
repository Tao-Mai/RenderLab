#pragma once

#include <entt/entt.hpp>

#include <type_traits>

namespace ecs
{
// Plain data types used as EnTT components. Prefer aggregates / movable types.
template <class T>
concept Component = std::is_object_v<T> && !std::is_reference_v<T> && !std::is_pointer_v<T>;
}
