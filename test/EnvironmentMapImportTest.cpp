#include "asset/AssetDataManager.h"
#include "asset/AssetDescManager.h"
#include "asset/AssetImporter.h"
#include "asset/TextureMip.h"
#include "core/ConfigManager.h"
#include "core/Context.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

namespace
{
struct ImportContext
{
    ConfigManager config;
    AssetDescManager descs;
    AssetDataManager data;

    ImportContext()
    {
        context().config = &config;
        context().assetDescManager = &descs;
        context().assetDataManager = &data;
        config.init();
        data.init();
        descs.init();
    }

    ~ImportContext()
    {
        descs.shutdown();
        config.shutdown();
        context().assetDataManager = nullptr;
        context().assetDescManager = nullptr;
        context().config = nullptr;
    }
};

struct ImportFiles
{
    std::filesystem::path root;
    std::string stem;

    ~ImportFiles()
    {
        std::error_code error;
        std::filesystem::remove(root / (stem + ".pnm"), error);
        std::filesystem::remove(root / "EnvironmentMap" / (stem + ".json"), error);
        for (const std::string suffix : {"", "_irradiance", "_prefiltered_specular"})
        {
            std::filesystem::remove(root / "Texture" / (stem + suffix + ".json"), error);
            std::filesystem::remove(root / "binary" / "texture" /
                                        (stem + suffix + ".bin"), error);
        }
    }
};

[[nodiscard]] float floatAt(const std::vector<uint8_t>& bytes, size_t pixel, size_t channel)
{
    float value;
    std::memcpy(&value, bytes.data() + pixel * 4 * sizeof(float) +
                channel * sizeof(float), sizeof(value));
    return value;
}
}

TEST(EnvironmentMapImportTest, BuildsRadianceMipsAndBakesFromThem)
{
    ImportContext assets;
    const std::string stem = "test_environment_mips_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const ImportFiles files{assets.config.paths().assets, stem};
    const std::filesystem::path source = files.root / (stem + ".pnm");

    std::ofstream output(source, std::ios::binary);
    ASSERT_TRUE(output);
    output << "P6\n32 16\n255\n";
    for (uint32_t y = 0; y < 16; ++y)
    {
        for (uint32_t x = 0; x < 32; ++x)
        {
            const std::array<uint8_t, 3> rgb{
                static_cast<uint8_t>(x * 7 + y),
                static_cast<uint8_t>(x * 4),
                static_cast<uint8_t>(y * 13),
            };
            output.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
        }
    }
    output.close();
    ASSERT_TRUE(output);

    ImportEnvironmentMapSetting setting{};
    setting.colorSpace = ColorSpace::Linear;
    setting.irradianceSize = 4;
    setting.irradianceSampleCount = 64;
    setting.prefilteredSpecularSampleCount = 64;
    const EnvironmentMap::ID environmentId =
        AssetImporter::importEnvironmentMap(source, setting);
    const EnvironmentMap::Desc& environment = assets.descs.desc(environmentId);

    const Texture::Desc& radiance = assets.descs.desc(environment.radiance.textureID);
    ASSERT_EQ(radiance.layout, ImageLayout::Cubemap);
    ASSERT_EQ(radiance.format, ImageFormat::RGBA8);
    ASSERT_EQ(radiance.width, 8u);
    ASSERT_EQ(radiance.mipLevels, 4u);
    const std::vector<uint8_t> radianceBytes = assets.data.readTexture(radiance);
    ASSERT_EQ(radianceBytes.size(), texture_mip::totalBytes(8, 8, 6, 4, 4));

    const size_t mip1Offset = texture_mip::offset(8, 8, 6, 4, 1);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < 4; ++y)
        {
            for (uint32_t x = 0; x < 4; ++x)
            {
                const size_t target = mip1Offset +
                    ((static_cast<size_t>(face) * 4 + y) * 4 + x) * 4;
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    uint32_t sum = 0;
                    for (uint32_t dy = 0; dy < 2; ++dy)
                    {
                        for (uint32_t dx = 0; dx < 2; ++dx)
                        {
                            const size_t sourcePixel =
                                ((static_cast<size_t>(face) * 8 + y * 2 + dy) * 8 +
                                    x * 2 + dx) * 4;
                            sum += radianceBytes[sourcePixel + channel];
                        }
                    }
                    EXPECT_NEAR(radianceBytes[target + channel], sum / 4.0f, 1.0f);
                }
            }
        }
    }

    const Texture::Desc& irradiance = assets.descs.desc(environment.irradiance.textureID);
    const Texture::Desc& prefiltered = assets.descs.desc(
        environment.prefilteredSpecular.textureID);
    ASSERT_EQ(irradiance.mipLevels, 1u);
    ASSERT_EQ(prefiltered.mipLevels, radiance.mipLevels);
    const std::vector<uint8_t> irradianceBytes = assets.data.readTexture(irradiance);
    const std::vector<uint8_t> prefilteredBytes = assets.data.readTexture(prefiltered);
    ASSERT_EQ(irradianceBytes.size(), texture_mip::totalBytes(4, 4, 6, 16, 1));
    ASSERT_EQ(prefilteredBytes.size(), texture_mip::totalBytes(8, 8, 6, 16, 4));

    for (const auto* bytes : {&irradianceBytes, &prefilteredBytes})
    {
        for (size_t pixel = 0; pixel < bytes->size() / 16; ++pixel)
        {
            for (size_t channel = 0; channel < 4; ++channel)
            {
                EXPECT_TRUE(std::isfinite(floatAt(*bytes, pixel, channel)));
            }
        }
    }
}
