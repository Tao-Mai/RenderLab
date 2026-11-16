#pragma once

#include "render/device/GpuUploadContext.h"

#include <cstdint>
#include <span>

#include <vulkan/vulkan_raii.hpp>

struct TextureAsset;

class GpuTexture
{
public:
    GpuTexture(
        GpuUploadContext upload, const TextureAsset& asset,
        std::span<const uint8_t> bytes);

    GpuTexture(const GpuTexture&)            = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    [[nodiscard]] vk::ImageView             imageView() const { return *view; }
    [[nodiscard]] vk::Image                 imageHandle() const { return *image; }
    [[nodiscard]] const vk::ImageCreateInfo& imageInfo() const { return creationInfo; }

private:
    vk::raii::DeviceMemory imageMemory  = nullptr;
    vk::raii::Image        image        = nullptr;
    vk::raii::ImageView    view         = nullptr;
    vk::ImageCreateInfo creationInfo;

    void create(
        GpuUploadContext upload,
        std::span<const uint8_t> bytes,
        const TextureAsset& asset);
};
