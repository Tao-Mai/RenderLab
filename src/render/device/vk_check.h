#pragma once

#include "logger.h"

#include <string_view>
#include <utility>

#include <vulkan/vulkan.hpp>

[[nodiscard]] inline vk::Result vkCheck(vk::Result result, std::string_view what)
{
    CHECK(result == vk::Result::eSuccess, "{} failed: {}", what, vk::to_string(result));
    return result;
}

template <class T>
[[nodiscard]] T vkCheck(vk::ResultValue<T> result, std::string_view what)
{
    CHECK(result.result == vk::Result::eSuccess, "{} failed: {}", what, vk::to_string(result.result));
    return std::move(result.value);
}
