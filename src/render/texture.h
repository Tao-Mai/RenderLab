#pragma once

#include "render/buffer.h"

#include <array>
#include <cstdint>
#include <filesystem>

#include <vulkan/vulkan_raii.hpp>

class Texture
{
  public:
    Texture(
        const vk::raii::PhysicalDevice &physicalDevice,
        const vk::raii::Device &device,
        const vk::raii::CommandPool &commandPool,
        vk::raii::Queue &queue,
        const std::filesystem::path &path);
    Texture(
        const vk::raii::PhysicalDevice &physicalDevice,
        const vk::raii::Device &device,
        const vk::raii::CommandPool &commandPool,
        vk::raii::Queue &queue,
        const std::array<uint8_t, 4> &rgba);

    Texture(const Texture &) = delete;
    Texture &operator=(const Texture &) = delete;

    [[nodiscard]] vk::ImageView imageView() const;
    [[nodiscard]] vk::Sampler sampler() const;

  private:
    vk::raii::DeviceMemory imageMemory = nullptr;
    vk::raii::Image image = nullptr;
    vk::raii::ImageView view = nullptr;
    vk::raii::Sampler imageSampler = nullptr;

    void create(
        const vk::raii::PhysicalDevice &physicalDevice,
        const vk::raii::Device &device,
        const vk::raii::CommandPool &commandPool,
        vk::raii::Queue &queue,
        const uint8_t *pixels,
        uint32_t width,
        uint32_t height);
    static uint32_t findMemoryType(
        const vk::raii::PhysicalDevice &physicalDevice,
        uint32_t typeFilter,
        vk::MemoryPropertyFlags properties);
};
