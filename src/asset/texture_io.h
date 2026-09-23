#pragma once

#include "asset/asset_desc.h"

#include <cstdint>
#include <filesystem>
#include <vector>

struct TexturePixels
{
    TextureDesc::DataFormat format;
    TextureDesc::Layout layout;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> bytes;
};

namespace texture_io
{
void write(const std::filesystem::path& file, const TexturePixels& pixels);
[[nodiscard]] TexturePixels read(const std::filesystem::path& file);
[[nodiscard]] uint32_t bytesPerPixel(TextureDesc::DataFormat format);
}
