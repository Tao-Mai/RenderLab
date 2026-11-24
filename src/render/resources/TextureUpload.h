#pragma once
#include <cstdint>
#include <slang-rhi.h>
#include <span>
struct TextureAsset;
[[nodiscard]] rhi::ComPtr<rhi::ITexture>
uploadTexture(rhi::IDevice& device, rhi::ICommandEncoder& commands, const TextureAsset& asset,
    std::span<const uint8_t> bytes);
