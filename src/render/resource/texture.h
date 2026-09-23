#pragma once

#include "asset/texture_io.h"
#include "render/device/gpu_upload_context.h"

#include <array>
#include <cstdint>

#include <vulkan/vulkan_raii.hpp>

class Texture
{
public:
    Texture(GpuUploadContext upload, const TexturePixels& pixels);
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
