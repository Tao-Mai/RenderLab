#pragma once

#include "asset/Asset.h"
#include "render/device/GpuUploadContext.h"

#include <cstdint>
#include <span>

#include <vulkan/vulkan_raii.hpp>

class GpuTexture
{
public:
    GpuTexture(
        GpuUploadContext upload, const Texture::Desc& desc,
        std::span<const uint8_t> bytes);

    GpuTexture(const GpuTexture&)            = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    [[nodiscard]] vk::ImageView imageView() const;

private:
    vk::raii::DeviceMemory imageMemory  = nullptr;
    vk::raii::Image        image        = nullptr;
    vk::raii::ImageView    view         = nullptr;

    void create(
        GpuUploadContext upload,
        std::span<const uint8_t> bytes,
        const Texture::Desc& desc,
        vk::Format format);
};
