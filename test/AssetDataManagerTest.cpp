#include "asset/AssetDataManager.h"
#include "asset/TextureMip.h"
#include "core/ConfigManager.h"
#include "core/Context.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>
#include <glm/vector_relational.hpp>

namespace
{
struct ConfiguredData
{
    ConfigManager config;
    AssetDataManager data;

    ConfiguredData()
    {
        context().config = &config;
        config.init();
        data.init();
    }

    ~ConfiguredData()
    {
        context().config = nullptr;
        config.shutdown();
    }
};

struct TemporaryDataFile
{
    std::filesystem::path path;
    std::filesystem::path root;

    ~TemporaryDataFile()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
        for (auto directory = path.parent_path(); directory != root;
             directory = directory.parent_path())
        {
            if (!std::filesystem::remove(directory, error))
            {
                break;
            }
        }
    }
};

[[nodiscard]] std::string uniqueId(const std::string& prefix)
{
    return prefix + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
}
}

TEST(AssetDataManagerTest, PreservesCubemapPixelsAndMetadata)
{
    ConfiguredData assets;

    Texture::Desc desc{};
    desc.id = uniqueId("test_cubemap_");
    desc.format = ImageFormat::RGBA8;
    desc.colorSpace = ColorSpace::Srgb;
    desc.layout = ImageLayout::Cubemap;
    desc.width  = 2;
    desc.height = 2;
    desc.mipLevels = 1;

    std::vector<uint8_t> pixels(6 * 2 * 2 * 4);
    for (size_t index = 0; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<uint8_t>(index);
    }

    const auto relative = std::filesystem::path{"binary/texture"} /
        (desc.id.value + ".bin");
    const TemporaryDataFile output{
        assets.config.paths().assets / relative, assets.config.paths().assets};
    desc.binary = assets.data.writeTexture(desc, pixels);
    EXPECT_EQ(*desc.binary, relative);
    EXPECT_EQ(assets.data.readTexture(desc), pixels);
}

TEST(AssetDataManagerTest, PreservesCubemapMipChain)
{
    ConfiguredData assets;

    Texture::Desc desc{};
    desc.id = uniqueId("test_mipped_cubemap_");
    desc.source = Source::File;
    desc.format = ImageFormat::RGBA32F;
    desc.colorSpace = ColorSpace::Linear;
    desc.layout = ImageLayout::Cubemap;
    desc.width = 4;
    desc.height = 4;
    desc.mipLevels = 3;

    constexpr uint32_t pixelBytes = 4 * sizeof(float);
    std::vector<uint8_t> pixels(texture_mip::totalBytes(4, 4, 6, pixelBytes, 3));
    for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
    {
        const size_t offset = texture_mip::offset(4, 4, 6, pixelBytes, mip);
        const size_t faceBytes = texture_mip::levelBytes(4, 4, 1, pixelBytes, mip);
        for (uint32_t face = 0; face < 6; ++face)
        {
            std::fill_n(pixels.begin() + offset + face * faceBytes,
                        faceBytes, static_cast<uint8_t>(mip * 6 + face));
        }
    }

    const auto relative = std::filesystem::path{"binary/texture"} /
        (desc.id.value + ".bin");
    const TemporaryDataFile output{
        assets.config.paths().assets / relative, assets.config.paths().assets};
    desc.binary = assets.data.writeTexture(desc, pixels);

    EXPECT_EQ(assets.data.readTexture(desc), pixels);
}

TEST(AssetDataManagerTest, PreservesMeshGeometry)
{
    ConfiguredData assets;
    const Mesh::ID id = uniqueId("test_mesh_");
    const auto relative = std::filesystem::path{"binary/geometry"} /
        (id.value + ".bin");
    const TemporaryDataFile output{
        assets.config.paths().assets / relative, assets.config.paths().assets};

    MeshGeometry mesh;
    mesh.vertices = {
        {{0.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{3.0f, 4.0f, 5.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{6.0f, 7.0f, 8.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    };
    mesh.indices = {0, 1, 2};

    const auto geometry = assets.data.writeGeometry(id, mesh);
    EXPECT_EQ(geometry, relative);

    Mesh::Desc desc{};
    desc.id       = id;
    desc.source   = Source::File;
    desc.geometry = geometry;

    const MeshGeometry loaded = assets.data.readGeometry(desc);
    ASSERT_EQ(loaded.vertices.size(), mesh.vertices.size());
    EXPECT_EQ(loaded.indices, mesh.indices);
    for (size_t index = 0; index < mesh.vertices.size(); ++index)
    {
        EXPECT_TRUE(glm::all(glm::equal(
            loaded.vertices[index].position, mesh.vertices[index].position)));
        EXPECT_TRUE(glm::all(glm::equal(
            loaded.vertices[index].normal, mesh.vertices[index].normal)));
        EXPECT_TRUE(glm::all(glm::equal(
            loaded.vertices[index].texcoord, mesh.vertices[index].texcoord)));
    }
}
