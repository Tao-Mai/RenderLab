#include "asset/AssetDataManager.h"
#include "asset/AssetDescManager.h"
#include "asset/AssetImporter.h"
#include "asset/TextureMip.h"
#include "core/ConfigManager.h"
#include "core/Context.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stop_token>
#include <string>
#include <system_error>
#include <utility>
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
    std::optional<std::filesystem::path> externalSource;

    ~ImportFiles()
    {
        std::error_code error;
        std::filesystem::remove(root / (stem + ".pnm"), error);
        std::filesystem::remove(root / (stem + ".hdr"), error);
        if (externalSource)
        {
            std::filesystem::remove(*externalSource, error);
        }
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

void writeConstantHdr(std::ofstream& output)
{
    output << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
    constexpr std::array<uint8_t, 4> rgbe{128, 64, 32, 129};
    for (uint32_t pixel = 0; pixel < 8; ++pixel)
    {
        output.write(reinterpret_cast<const char*>(rgbe.data()), rgbe.size());
    }
}
}

TEST(EnvironmentMapImportTest, ImportsHdrTextureAsLinearFloat)
{
    ImportContext assets;
    const std::string stem = "test_hdr_texture_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const ImportFiles files{assets.config.paths().assets, stem};
    const std::filesystem::path source = files.root / (stem + ".hdr");

    std::ofstream output(source, std::ios::binary);
    ASSERT_TRUE(output);
    writeConstantHdr(output);
    output.close();
    ASSERT_TRUE(output);

    const Texture::ID id = AssetImporter::importTexture({.source = source});
    const Texture::Desc& texture = assets.descs.desc(id);
    EXPECT_EQ(texture.format, ImageFormat::RGBA32F);
    EXPECT_EQ(texture.colorSpace, ColorSpace::Linear);
    EXPECT_EQ(texture.layout, ImageLayout::Image2D);
    EXPECT_EQ(texture.width, 4u);
    EXPECT_EQ(texture.height, 2u);

    const std::vector<uint8_t> bytes = assets.data.readTexture(texture);
    ASSERT_EQ(bytes.size(), 4u * 2u * 4u * sizeof(float));
    EXPECT_NEAR(floatAt(bytes, 0, 0), 1.0f, 0.01f);
    EXPECT_NEAR(floatAt(bytes, 0, 1), 0.5f, 0.01f);
    EXPECT_NEAR(floatAt(bytes, 0, 2), 0.25f, 0.01f);
    EXPECT_NEAR(floatAt(bytes, 0, 3), 1.0f, 0.01f);
}

TEST(EnvironmentMapImportTest, BakesHdrEnvironment)
{
    ImportContext assets;
    const std::string stem = "test_hdr_environment_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const ImportFiles files{assets.config.paths().assets, stem};
    const std::filesystem::path source = files.root / (stem + ".hdr");

    std::ofstream output(source, std::ios::binary);
    ASSERT_TRUE(output);
    writeConstantHdr(output);
    output.close();
    ASSERT_TRUE(output);

    ImportEnvironmentMapSetting setting{};
    setting.source = source;
    setting.irradianceSize = 2;
    setting.irradianceSampleCount = 16;
    setting.prefilteredSpecularSampleCount = 16;
    setting.prefilteredSpecularMaxSampleCount = 16;
    const EnvironmentMap::ID id = AssetImporter::importEnvironmentMap(setting);
    const EnvironmentMap::Desc& environment = assets.descs.desc(id);
    const Texture::Desc& radiance = assets.descs.desc(environment.radiance.textureID);
    const Texture::Desc& irradiance = assets.descs.desc(environment.irradiance.textureID);
    const Texture::Desc& prefiltered = assets.descs.desc(
        environment.prefilteredSpecular.textureID);

    EXPECT_EQ(radiance.format, ImageFormat::RGBA32F);
    EXPECT_EQ(radiance.colorSpace, ColorSpace::Linear);
    EXPECT_EQ(radiance.layout, ImageLayout::Cubemap);
    EXPECT_EQ(irradiance.format, ImageFormat::RGBA32F);
    EXPECT_EQ(prefiltered.format, ImageFormat::RGBA32F);

    const std::vector<uint8_t> radianceBytes = assets.data.readTexture(radiance);
    ASSERT_EQ(radianceBytes.size(), texture_mip::totalBytes(1, 1, 6, 16, 1));
    EXPECT_NEAR(floatAt(radianceBytes, 0, 0), 1.0f, 0.01f);
    EXPECT_NEAR(floatAt(radianceBytes, 0, 1), 0.5f, 0.01f);
    EXPECT_NEAR(floatAt(radianceBytes, 0, 2), 0.25f, 0.01f);
}

TEST(EnvironmentMapImportTest, ParallelBakeMatchesPreparedOutput)
{
    ImportContext assets;
    const std::string stem = "test_parallel_environment_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const ImportFiles files{assets.config.paths().assets, stem};
    const std::filesystem::path source = files.root / (stem + ".pnm");

    std::ofstream output(source, std::ios::binary);
    ASSERT_TRUE(output);
    output << "P6\n48 24\n255\n";
    for (uint32_t y = 0; y < 24; ++y)
    {
        for (uint32_t x = 0; x < 48; ++x)
        {
            const std::array<uint8_t, 3> rgb{
                static_cast<uint8_t>(x * 5),
                static_cast<uint8_t>(y * 9),
                static_cast<uint8_t>((x + y) * 3),
            };
            output.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
        }
    }
    output.close();
    ASSERT_TRUE(output);

    ImportEnvironmentMapSetting setting{};
    setting.source = source;
    setting.irradianceSize = 6;
    setting.irradianceSampleCount = 32;
    setting.prefilteredSpecularSampleCount = 32;
    setting.prefilteredSpecularMaxSampleCount = 64;

    auto first = AssetImporter::prepareEnvironmentMap(setting);
    auto second = AssetImporter::prepareEnvironmentMap(setting);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->radianceBytes, second->radianceBytes);
    EXPECT_EQ(first->irradianceBytes, second->irradianceBytes);
    EXPECT_EQ(first->prefilteredBytes, second->prefilteredBytes);

    std::stop_source cancellation;
    cancellation.request_stop();
    EXPECT_FALSE(AssetImporter::prepareEnvironmentMap(
        setting, cancellation.get_token()).has_value());

    const EnvironmentMap::ID id = AssetImporter::saveEnvironmentMap(std::move(*first));
    const EnvironmentMap::Desc& environment = assets.descs.desc(id);
    const Texture::Desc& prefiltered = assets.descs.desc(
        environment.prefilteredSpecular.textureID);
    EXPECT_EQ(assets.data.readTexture(prefiltered), second->prefilteredBytes);
}

TEST(EnvironmentMapImportTest, ImportsTextureOutsideProject)
{
    ImportContext assets;
    const std::string stem = "test_external_texture_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path source =
        std::filesystem::temp_directory_path() / (stem + ".pnm");
    const ImportFiles files{assets.config.paths().assets, stem, source};

    std::ofstream output(source, std::ios::binary);
    ASSERT_TRUE(output);
    output << "P6\n1 1\n255\n";
    const std::array<uint8_t, 3> rgb{12, 34, 56};
    output.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    output.close();
    ASSERT_TRUE(output);

    const Texture::ID id = AssetImporter::importTexture({.source = source});
    const Texture::Desc& texture = assets.descs.desc(id);
    ASSERT_TRUE(texture.path);
    EXPECT_EQ(*texture.path, source);
    EXPECT_EQ(assets.data.readTexture(texture),
              std::vector<uint8_t>(rgb.begin(), rgb.end()));
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
    setting.source = source;
    setting.colorSpace = ColorSpace::Linear;
    setting.irradianceSize = 4;
    setting.irradianceSampleCount = 64;
    setting.prefilteredSpecularSampleCount = 64;
    setting.prefilteredSpecularMaxSampleCount = 64;
    const EnvironmentMap::ID environmentId =
        AssetImporter::importEnvironmentMap(setting);
    const EnvironmentMap::Desc& environment = assets.descs.desc(environmentId);
    const auto environmentIds = assets.descs.loadedIds<EnvironmentMap>();
    EXPECT_NE(std::ranges::find(environmentIds, environmentId), environmentIds.end());

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
