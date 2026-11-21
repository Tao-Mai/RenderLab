#pragma once

#include "core/Macros.h"
#include "render/resources/Formats.h"
#include "render/resources/Resource.h"

#include <cstdint>
#include <map>
#include <tuple>

#include <vulkan/vulkan_raii.hpp>

class VulkanContext;

namespace Falcor
{
class FALCOR_API Texture : public Resource
{
    FALCOR_OBJECT(Texture)

public:
    Texture(const VulkanContext& vulkan, const vk::ImageCreateInfo& info, ResourceBindFlags bindFlags);
    Texture(const VulkanContext& vulkan, vk::Image image, const vk::ImageCreateInfo& info, ResourceBindFlags bindFlags);
    ~Texture() override;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    [[nodiscard]] vk::Image     handle() const { return image; }
    [[nodiscard]] uint32_t      getWidth() const { return info.extent.width; }
    [[nodiscard]] uint32_t      getHeight() const { return info.extent.height; }
    [[nodiscard]] uint32_t      getDepth() const { return info.extent.depth; }
    [[nodiscard]] uint32_t      getArraySize() const
    {
        return getType() == Type::TextureCube ? info.arrayLayers / 6 : info.arrayLayers;
    }
    [[nodiscard]] uint32_t      getMipCount() const { return info.mipLevels; }
    [[nodiscard]] uint32_t      getSampleCount() const { return static_cast<uint32_t>(info.samples); }
    [[nodiscard]] Falcor::ResourceFormat getFormat() const;
    [[nodiscard]] ref<UnorderedAccessView> getUAV() override;
    [[nodiscard]] ref<UnorderedAccessView>
    getUAV(uint32_t mipLevel, uint32_t firstArraySlice = 0, uint32_t arraySize = kMaxPossible);
    [[nodiscard]] const vk::ImageCreateInfo& imageInfo() const { return info; }
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

    vk::ImageCreateInfo                 info;
    vk::raii::DeviceMemory              memory     = nullptr;
    vk::raii::Image                     ownedImage = nullptr;
    vk::Image                          image;
    std::map<ViewKey, vk::raii::ImageView> views;
};
}  // namespace Falcor
