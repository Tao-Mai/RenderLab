#pragma once

#include <cstdint>
#include <vulkan/vulkan_enums.hpp>

inline constexpr uint32_t maxFramesInFlight = 2;
inline constexpr uint32_t kMaxPointLightShadow = 4;
inline constexpr uint32_t kMaxRectAreaLightShadow = 4;
inline constexpr vk::Format kHdrColorFormat = vk::Format::eR16G16B16A16Sfloat;
