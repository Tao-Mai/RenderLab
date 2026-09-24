#pragma once

#include "asset/AssetDesc.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace texture_io
{
void write(
    const std::filesystem::path& file, const TextureDesc& desc,
    std::span<const uint8_t> bytes);
[[nodiscard]] std::vector<uint8_t> read(
    const std::filesystem::path& file, const TextureDesc& desc);
[[nodiscard]] uint32_t bytesPerPixel(TextureDesc::DataFormat format);
}
