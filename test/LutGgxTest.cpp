#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

TEST(LutGgxTest, StoresTwoValuesInEightBitRedAndGreenChannels)
{
    const auto path = std::filesystem::path{RENDERLAB_SOURCE_DIR} / "resource/lut_ggx.png";

    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    std::array<uint8_t, 26> header{};
    file.read(reinterpret_cast<char*>(header.data()), header.size());
    ASSERT_EQ(file.gcount(), static_cast<std::streamsize>(header.size()));

    constexpr std::array<uint8_t, 8> pngSignature{137, 80, 78, 71, 13, 10, 26, 10};
    ASSERT_TRUE(std::equal(pngSignature.begin(), pngSignature.end(), header.begin()));
    ASSERT_EQ(std::string_view(reinterpret_cast<const char*>(header.data() + 12), 4), "IHDR");
    EXPECT_EQ(header[24], 8); // PNG sample bit depth
    EXPECT_EQ(header[25], 2); // PNG color type 2 = RGB

    int width = 0;
    int height = 0;
    int channels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(path.string().c_str(), &width, &height, &channels, 0),
        stbi_image_free);
    ASSERT_NE(pixels, nullptr) << stbi_failure_reason();
    ASSERT_EQ(channels, 3);

    uint8_t redMax = 0;
    uint8_t greenMax = 0;
    uint8_t blueMax = 0;
    const auto pixelCount = static_cast<size_t>(width) * height;
    for (size_t index = 0; index < pixelCount; ++index)
    {
        redMax = std::max(redMax, pixels.get()[index * 3]);
        greenMax = std::max(greenMax, pixels.get()[index * 3 + 1]);
        blueMax = std::max(blueMax, pixels.get()[index * 3 + 2]);
    }

    EXPECT_GT(redMax, 0);
    EXPECT_GT(greenMax, 0);
    EXPECT_EQ(blueMax, 0);
}
