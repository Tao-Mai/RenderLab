#include "asset/TextureIo.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace
{
struct TemporaryFile
{
    std::filesystem::path path;

    ~TemporaryFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
}

TEST(TextureIoTest, PreservesCubemapPixelsAndMetadata)
{
    const auto name = "renderlab_texture_io_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".bin";
    const TemporaryFile file{std::filesystem::temp_directory_path() / name};

    TextureDesc desc{};
    desc.format = ImageFormat::Rgba8Srgb;
    desc.layout = TextureDesc::Layout::Cubemap;
    desc.width  = 2;
    desc.height = 2;

    std::vector<uint8_t> pixels(6 * 2 * 2 * 4);
    for (size_t index = 0; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<uint8_t>(index);
    }

    texture_io::write(file.path, desc, pixels);
    EXPECT_EQ(texture_io::read(file.path, desc), pixels);
}
