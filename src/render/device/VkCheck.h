#pragma once

#include "core/Logger.h"

#include <string_view>
#include <utility>

#include <vulkan/vulkan.hpp>

namespace vk_detail
{
inline void check(
    vk::Result result, std::string_view expression,
    const char* file, int line, const char* function)
{
    if (result != vk::Result::eSuccess)
    {
        logger::detail::fatal(
            file, line, "Vulkan call '{}' in {} failed: {}",
            expression, function, vk::to_string(result));
    }
}

template <class T>
[[nodiscard]] T check(
    vk::ResultValue<T> result, std::string_view expression,
    const char* file, int line, const char* function)
{
    check(result.result, expression, file, line, function);
    return std::move(result.value);
}
}

#define vkCheck(...) \
    ::vk_detail::check((__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__, __func__)
