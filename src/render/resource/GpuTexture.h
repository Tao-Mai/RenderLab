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
        GpuUploadContext upload, const TextureAsset& asset,
        std::span<const uint8_t> bytes);

    GpuTexture(const GpuTexture&)            = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;

    [[nodiscard]] vk::ImageView imageView() const;
    [[nodiscard]] vk::Image imageHandle() const;
    [[nodiscard]] const vk::ImageCreateInfo& imageInfo() const;

private:
    vk::raii::DeviceMemory imageMemory  = nullptr;
    vk::raii::Image        image        = nullptr;
    vk::raii::ImageView    view         = nullptr;
    vk::ImageCreateInfo creationInfo;

    void create(
        GpuUploadContext upload,
        std::span<const uint8_t> bytes,
        const TextureAsset& asset,
        vk::Format format);
};
