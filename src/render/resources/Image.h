//
// Created by 24500 on 2026/10/6.
//

#pragma once

#include <cstdint>
#include <map>
#include <tuple>

#include <vulkan/vulkan_raii.hpp>

// Borrows the image and device; owns the cached image views.
class Image
{
public:
    Image(const vk::raii::Device& device, vk::Image image);

    [[nodiscard]] vk::Image     handle() const;
    [[nodiscard]] vk::ImageView getOrCreateImageView(const vk::ImageViewCreateInfo& viewInfo);

private:
    using ViewKey = std::tuple<
        vk::ImageViewCreateFlags,
        vk::ImageViewType,
        vk::Format,
        vk::ComponentSwizzle,
        vk::ComponentSwizzle,
        vk::ComponentSwizzle,
        vk::ComponentSwizzle,
        vk::ImageAspectFlags,
        uint32_t,
        uint32_t,
        uint32_t,
        uint32_t>;

    const vk::raii::Device*                device;
    vk::Image                              image;
    std::map<ViewKey, vk::raii::ImageView> views;
};
