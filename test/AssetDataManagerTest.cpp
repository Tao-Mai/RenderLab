#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
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

#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>
#include <gtest/gtest.h>

namespace
{
struct ConfiguredData
{
    ConfigManager    config;
    AssetDataManager data;

    ConfiguredData()
    {
        context().config = &config;
        config.init();
        data.init(config.paths().assets);
    }

    ~ConfiguredData()
    {
        data.shutdown();
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
        for (auto directory = path.parent_path(); directory != root; directory = directory.parent_path())
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
    return prefix + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}
}  // namespace

TEST(AssetDataManagerTest, TangentsSplitSharedVerticesAcrossMirroredUvs)
{
    MeshGeometry mesh;
    mesh.vertices = {
        {{0, 0, 0}, {0, 0, 1}, {0, 0}},
        {{1, 0, 0}, {0, 0, 1}, {1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {0, 1}},
        {{0, -1, 0}, {0, 0, 1}, {0, 1}},
    };
    mesh.indices = {0, 1, 2, 0, 3, 1};
    generateTangents(mesh, 0, static_cast<uint32_t>(mesh.indices.size()));

    EXPECT_GT(mesh.vertices.size(), 4u);
    const auto& first    = mesh.vertices[mesh.indices[0]];
    const auto& mirrored = mesh.vertices[mesh.indices[3]];
    EXPECT_NE(mesh.indices[0], mesh.indices[3]);
    EXPECT_FLOAT_EQ(first.tangent.w, 1.0f);
    EXPECT_FLOAT_EQ(mirrored.tangent.w, -1.0f);
    EXPECT_NEAR(glm::dot(first.normal, glm::vec3(first.tangent)), 0.0f, 1e-6f);
}

TEST(AssetDataManagerTest, BuiltinFlatNormalUsesLinearNeutralPixels)
{
    AssetManager     assets;
    AssetDataManager data;
    const auto&      normal = assets.get<TextureAsset>(BuiltinAssets::Texture::flatNormal);

    EXPECT_EQ(normal.colorSpace, ColorSpace::Linear);
    EXPECT_EQ(normal.format, ImageFormat::RGBA8);
    EXPECT_EQ(normal.width, 1u);
    EXPECT_EQ(normal.height, 1u);
    EXPECT_EQ(data.readTexture(normal), (std::vector<uint8_t>{128, 128, 255, 255}));
}

TEST(AssetDataManagerTest, BuiltinLutsProvidePixelsWithoutPersistedData)
{
    AssetManager     assets;
    AssetDataManager data;
    for (const auto& id : {BuiltinAssets::Texture::kBrdfLut, BuiltinAssets::Texture::kSheenLut})
    {
        const auto& texture = assets.get(id);
        const auto  pixels  = data.readTexture(texture);
        EXPECT_EQ(
            pixels.size(),
            static_cast<size_t>(texture.width) * texture.height * AssetDataManager::bytesPerPixel(texture.format)
        );
        EXPECT_TRUE(std::ranges::any_of(pixels, [](uint8_t value) { return value != 0; }));
    }
}

TEST(AssetDataManagerTest, PreservesCubemapPixelsAndMetadata)
{
    ConfiguredData assets;

    TextureAsset desc{};
    desc.id         = uniqueId("test_cubemap_");
    desc.format     = ImageFormat::RGBA8;
    desc.colorSpace = ColorSpace::Srgb;
    desc.layout     = ImageLayout::Cubemap;
    desc.width      = 2;
    desc.height     = 2;
    desc.mipLevels  = 1;

    std::vector<uint8_t> pixels(6 * 2 * 2 * 4);
    for (size_t index = 0; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<uint8_t>(index);
    }

    const auto              relative = std::filesystem::path{"binary/texture"} / (desc.id.value + ".bin");
    const TemporaryDataFile output{assets.config.paths().assets / relative, assets.config.paths().assets};
    desc.binary = assets.data.writeTexture(desc, pixels);
    EXPECT_EQ(*desc.binary, relative);
    EXPECT_EQ(assets.data.readTexture(desc), pixels);
}

TEST(AssetDataManagerTest, PreservesCubemapMipChain)
{
    ConfiguredData assets;

    TextureAsset desc{};
    desc.id         = uniqueId("test_mipped_cubemap_");
    desc.source     = Source::File;
    desc.format     = ImageFormat::RGBA32F;
    desc.colorSpace = ColorSpace::Linear;
    desc.layout     = ImageLayout::Cubemap;
    desc.width      = 4;
    desc.height     = 4;
    desc.mipLevels  = 3;

    constexpr uint32_t   pixelBytes = 4 * sizeof(float);
    std::vector<uint8_t> pixels(texture_mip::totalBytes(4, 4, 6, pixelBytes, 3));
    for (uint32_t mip = 0; mip < desc.mipLevels; ++mip)
    {
        const size_t offset    = texture_mip::offset(4, 4, 6, pixelBytes, mip);
        const size_t faceBytes = texture_mip::levelBytes(4, 4, 1, pixelBytes, mip);
        for (uint32_t face = 0; face < 6; ++face)
        {
            std::fill_n(pixels.begin() + offset + face * faceBytes, faceBytes, static_cast<uint8_t>(mip * 6 + face));
        }
    }

    const auto              relative = std::filesystem::path{"binary/texture"} / (desc.id.value + ".bin");
    const TemporaryDataFile output{assets.config.paths().assets / relative, assets.config.paths().assets};
    desc.binary = assets.data.writeTexture(desc, pixels);

    EXPECT_EQ(assets.data.readTexture(desc), pixels);
}

TEST(AssetDataManagerTest, BuiltinSphereWindingMatchesOutwardNormals)
{
    AssetDataManager data;
    MeshAsset        asset;
    asset.id     = BuiltinAssets::Mesh::sphere;
    asset.source = Source::Builtin;
    const auto mesh = data.readGeometry(asset);

    constexpr float kMinAreaSquared = 1e-16f;
    size_t checkedTriangles = 0;
    for (size_t index = 0; index < mesh.indices.size(); index += 3)
    {
        const auto& a = mesh.vertices[mesh.indices[index]];
        const auto& b = mesh.vertices[mesh.indices[index + 1]];
        const auto& c = mesh.vertices[mesh.indices[index + 2]];
        const auto geometricNormal = glm::cross(b.position - a.position, c.position - a.position);
        // Pole triangles have zero area and no defined winding.
        if (glm::dot(geometricNormal, geometricNormal) <= kMinAreaSquared)
            continue;

        ASSERT_GT(glm::dot(geometricNormal, a.normal + b.normal + c.normal), 0.0f) << index / 3;
        ASSERT_GT(glm::dot(geometricNormal, a.position + b.position + c.position), 0.0f) << index / 3;
        ++checkedTriangles;
    }
    EXPECT_GT(checkedTriangles, 0u);
}

TEST(AssetDataManagerTest, BuiltinConeHasOutwardWindingAndUsableTangents)
{
    AssetManager     assets;
    AssetDataManager data;
    const auto&      asset = assets.get<MeshAsset>(BuiltinAssets::Mesh::kCone);
    const auto       mesh  = data.readGeometry(asset);
    ASSERT_FALSE(mesh.indices.empty());
    ASSERT_EQ(mesh.indices.size() % 3, 0u);
    EXPECT_EQ(asset.bounds.min, glm::vec3(-1.0f));
    EXPECT_EQ(asset.bounds.max, glm::vec3(1.0f, 1.0f, 0.0f));
    ASSERT_EQ(asset.submeshes.size(), 2u);
    EXPECT_EQ(asset.submeshes[0].firstIndex, 0u);
    EXPECT_EQ(asset.submeshes[1].firstIndex, asset.submeshes[0].indexCount);
    EXPECT_EQ(asset.submeshes[0].indexCount + asset.submeshes[1].indexCount, mesh.indices.size());
    for (uint32_t index = 0; index < asset.submeshes[0].indexCount; index += 3)
        EXPECT_FLOAT_EQ(mesh.vertices[mesh.indices[index]].position.z, 0.0f);
    for (uint32_t index = asset.submeshes[1].firstIndex; index < mesh.indices.size(); ++index)
        EXPECT_FLOAT_EQ(mesh.vertices[mesh.indices[index]].position.z, -1.0f);

    for (size_t index = 0; index < mesh.indices.size(); index += 3)
    {
        const auto& a               = mesh.vertices[mesh.indices[index]];
        const auto& b               = mesh.vertices[mesh.indices[index + 1]];
        const auto& c               = mesh.vertices[mesh.indices[index + 2]];
        const auto  geometricNormal = glm::cross(b.position - a.position, c.position - a.position);
        EXPECT_GT(glm::dot(geometricNormal, a.normal + b.normal + c.normal), 0.0f) << index / 3;
    }
    for (const auto& vertex : mesh.vertices)
    {
        EXPECT_NEAR(glm::length(vertex.normal), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(glm::vec3(vertex.tangent)), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::dot(vertex.normal, glm::vec3(vertex.tangent)), 0.0f, 1e-5f);
        EXPECT_TRUE(vertex.tangent.w == 1.0f || vertex.tangent.w == -1.0f);
    }
}

TEST(AssetDataManagerTest, PreservesMeshGeometry)
{
    ConfiguredData          assets;
    const MeshAsset::ID     id       = uniqueId("test_mesh_");
    const auto              relative = std::filesystem::path{"binary/geometry"} / (id.value + ".bin");
    const TemporaryDataFile output{assets.config.paths().assets / relative, assets.config.paths().assets};

    MeshGeometry mesh;
    mesh.vertices = {
        {{0.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{3.0f, 4.0f, 5.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{6.0f, 7.0f, 8.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
    };
    mesh.indices             = {0, 1, 2};
    mesh.vertices[0].tangent = {0.0f, 1.0f, 0.0f, -1.0f};

    const auto geometry = assets.data.writeGeometry(id, mesh);
    EXPECT_EQ(geometry, relative);

    MeshAsset desc{};
    desc.id       = id;
    desc.source   = Source::File;
    desc.geometry = geometry;

    const MeshGeometry loaded = assets.data.readGeometry(desc);
    ASSERT_EQ(loaded.vertices.size(), mesh.vertices.size());
    EXPECT_EQ(loaded.indices, mesh.indices);
    for (size_t index = 0; index < mesh.vertices.size(); ++index)
    {
        EXPECT_TRUE(glm::all(glm::equal(loaded.vertices[index].position, mesh.vertices[index].position)));
        EXPECT_TRUE(glm::all(glm::equal(loaded.vertices[index].normal, mesh.vertices[index].normal)));
        EXPECT_TRUE(glm::all(glm::equal(loaded.vertices[index].texcoord, mesh.vertices[index].texcoord)));
        EXPECT_EQ(loaded.vertices[index].tangent, mesh.vertices[index].tangent);
    }
}
