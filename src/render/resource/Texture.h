#pragma once

#include "asset/AssetDesc.h"
#include "render/device/GpuUploadContext.h"

#include <array>
#include <cstdint>
#include <span>

#include <vulkan/vulkan_raii.hpp>

class Texture
{
public:
    Texture(
        GpuUploadContext upload, const TextureDesc& desc,
        std::span<const uint8_t> bytes);
    Texture(GpuUploadContext upload, const std::array<uint8_t, 4>& rgba);

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    [[nodiscard]] vk::ImageView imageView() const;
    [[nodiscard]] vk::Sampler   sampler() const;

private:
    vk::raii::DeviceMemory imageMemory  = nullptr;
    vk::raii::Image        image        = nullptr;
    vk::raii::ImageView    view         = nullptr;
    vk::raii::Sampler      imageSampler = nullptr;

    void create(
        GpuUploadContext upload,
        const void* pixels,
        uint32_t width,
        uint32_t height,
        vk::Format format,
        uint32_t bytesPerPixel,
        TextureDesc::Layout layout);
};
