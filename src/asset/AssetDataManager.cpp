#include "asset/AssetDataManager.h"

#include "asset/BuiltinAssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"

#include <array>
#include <fstream>
#include <limits>
#include <type_traits>

namespace
{
constexpr uint32_t geometryMagic   = 0x4853454du;
constexpr uint32_t geometryVersion = 1;

constexpr uint32_t textureMagic   = 0x58455452u; // RTEX
constexpr uint32_t textureVersion = 1;

struct GeometryHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t vertexCount;
    uint32_t indexCount;
};

static_assert(sizeof(GeometryHeader) == 4 * sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<GeometryHeader>);

struct TextureHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t layout;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t layerCount;
    uint32_t payloadBytes;
};

static_assert(sizeof(TextureHeader) == 8 * sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<TextureHeader>);

void validateId(const AssetId& id)
{
    CHECK(!id.empty() && std::filesystem::path{id}.filename() == std::filesystem::path{id} &&
          id != "." && id != "..",
          "invalid asset data id: {}",
          id);
}

[[nodiscard]] uint32_t layerCount(ImageLayout layout)
{
    switch (layout)
    {
        case ImageLayout::Image2D:
            return 1;
        case ImageLayout::Cubemap:
            return 6;
    }

    LOG_FATAL("invalid texture layout");
}

void validateTexture(
    const TextureDesc&           desc, std::span<const uint8_t> bytes,
    const std::filesystem::path& file)
{
    CHECK(desc.width > 0 && desc.height > 0,
          "invalid texture dimensions: {}",
          file.string());
    CHECK(desc.layout != ImageLayout::Cubemap || desc.width == desc.height,
          "cubemap faces must be square: {}",
          file.string());

    const uint64_t maxPayload = std::numeric_limits<uint32_t>::max();
    const uint32_t pixelBytes = AssetDataManager::bytesPerPixel(desc.format);

    CHECK(desc.width <= maxPayload / desc.height / layerCount(desc.layout) / pixelBytes,
          "texture payload is too large: {}",
          file.string());

    const uint64_t expected = static_cast<uint64_t>(desc.width) * desc.height *
        layerCount(desc.layout) * pixelBytes;

    CHECK(expected == bytes.size(),
          "invalid texture payload size: {}",
          file.string());
}
}

void AssetDataManager::init()
{
    CHECK(context().config != nullptr,
          "ConfigManager must exist before AssetDataManager");

    const AppPaths& paths = context().config->paths();
    CHECK(!paths.assets.empty() && paths.assets.is_absolute() &&
          !paths.geometry.empty() && paths.geometry.is_absolute(),
          "AssetDataManager requires resolved asset paths");

    assetsRoot   = paths.assets;
    geometryRoot = paths.geometry;
}

std::filesystem::path AssetDataManager::resolve(
    const std::filesystem::path& relative) const
{
    CHECK(!assetsRoot.empty(), "AssetDataManager is not initialized");
    CHECK(!relative.empty() && !relative.is_absolute(),
          "asset data path must be relative: {}",
          relative.string());

    return (assetsRoot / relative).lexically_normal();
}

uint32_t AssetDataManager::bytesPerPixel(ImageFormat format)
{
    switch (format)
    {
        case ImageFormat::R8:
            return 1;
        case ImageFormat::RG8:
            return 2;
        case ImageFormat::RGB8:
            return 3;
        case ImageFormat::RGBA8:
            return 4;
        case ImageFormat::RGBA16F:
            return 8;
        case ImageFormat::RGBA32F:
            return 16;
    }

    LOG_FATAL("invalid texture data format");
}

std::filesystem::path AssetDataManager::writeGeometry(
    const AssetId& id, const MeshGeometry& mesh) const
{
    CHECK(!geometryRoot.empty(), "AssetDataManager is not initialized");
    validateId(id);

    const auto file = geometryRoot / (id + ".bin");
    CHECK(!mesh.vertices.empty() && !mesh.indices.empty(),
          "imported mesh has no geometry: {}",
          file.string());

    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    CHECK(output, "cannot write mesh geometry: {}", file.string());

    const GeometryHeader header{
        .magic = geometryMagic,
        .version = geometryVersion,
        .vertexCount = static_cast<uint32_t>(mesh.vertices.size()),
        .indexCount = static_cast<uint32_t>(mesh.indices.size()),
    };

    output.write(reinterpret_cast<const char*>(&header), sizeof(header));

    for (const Vertex& vertex : mesh.vertices)
    {
        const std::array<float, 8> values{
            vertex.position.x, vertex.position.y, vertex.position.z,
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            vertex.texcoord.x, vertex.texcoord.y,
        };
        output.write(reinterpret_cast<const char*>(values.data()), sizeof(values));
    }

    output.write(
        reinterpret_cast<const char*>(mesh.indices.data()),
        static_cast<std::streamsize>(mesh.indices.size() * sizeof(uint32_t)));

    CHECK(output, "failed writing mesh geometry: {}", file.string());

    return std::filesystem::relative(file, assetsRoot);
}

MeshGeometry AssetDataManager::readGeometry(const std::filesystem::path& relative) const
{
    if (relative.parent_path() == kBuiltinGeometryDir)
    {
        const BuiltinAssetManager* builtin = context().builtinAssetManager;
        CHECK(builtin != nullptr, "BuiltinAssetManager is not initialized");
        return builtin->meshGeometry(relative);
    }

    const auto    file = resolve(relative);
    std::ifstream input(file, std::ios::binary);
    CHECK(input, "cannot open mesh geometry: {}", file.string());

    GeometryHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    CHECK(input && header.magic == geometryMagic && header.version == geometryVersion,
          "invalid mesh geometry header: {}",
          file.string());

    const uint64_t expectedSize = sizeof(header) +
        static_cast<uint64_t>(header.vertexCount) * 8 * sizeof(float) +
        static_cast<uint64_t>(header.indexCount) * sizeof(uint32_t);

    CHECK(std::filesystem::file_size(file) == expectedSize &&
          header.vertexCount != 0 && header.indexCount != 0,
          "invalid mesh geometry size: {}",
          file.string());

    MeshGeometry geometry;
    geometry.vertices.reserve(header.vertexCount);
    geometry.indices.resize(header.indexCount);

    for (uint32_t index = 0; index < header.vertexCount; ++index)
    {
        std::array<float, 8> values{};
        input.read(reinterpret_cast<char*>(values.data()), sizeof(values));
        geometry.vertices.push_back({
            .position = {values[0], values[1], values[2]},
            .normal = {values[3], values[4], values[5]},
            .texcoord = {values[6], values[7]},
        });
    }

    input.read(
        reinterpret_cast<char*>(geometry.indices.data()),
        static_cast<std::streamsize>(geometry.indices.size() * sizeof(uint32_t)));

    CHECK(input, "failed reading mesh geometry: {}", file.string());

    return geometry;
}

std::filesystem::path AssetDataManager::writeTexture(
    const TextureDesc& desc, std::span<const uint8_t> bytes) const
{
    validateId(desc.id);

    const auto relative = std::filesystem::path{"binary/texture"} / (desc.id + ".bin");
    const auto file     = resolve(relative);
    validateTexture(desc, bytes, file);

    std::filesystem::create_directories(file.parent_path());
    std::filesystem::path temporary = file;
    temporary                       += ".tmp";

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    CHECK(output, "cannot write texture binary: {}", temporary.string());

    const TextureHeader header{
        .magic = textureMagic,
        .version = textureVersion,
        .layout = static_cast<uint32_t>(desc.layout),
        .format = static_cast<uint32_t>(desc.format),
        .width = desc.width,
        .height = desc.height,
        .layerCount = layerCount(desc.layout),
        .payloadBytes = static_cast<uint32_t>(bytes.size()),
    };

    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();

    CHECK(output, "failed writing texture binary: {}", temporary.string());

    std::error_code error;
    std::filesystem::rename(temporary, file, error);

    CHECK(!error,
          "failed replacing texture binary '{}': {}",
          file.string(),
          error.message());

    return relative;
}

std::vector<uint8_t> AssetDataManager::readTexture(const TextureDesc& desc) const
{
    if (desc.source == kBuiltinTextureSource)
    {
        const BuiltinAssetManager* builtin = context().builtinAssetManager;
        CHECK(builtin != nullptr, "BuiltinAssetManager is not initialized");
        std::vector<uint8_t> bytes = builtin->textureData(desc.id);
        validateTexture(desc, bytes, desc.id);
        return bytes;
    }

    CHECK(desc.binary.has_value() && !desc.binary->empty(),
          "texture '{}' has no binary path",
          desc.id);

    const auto    file = resolve(*desc.binary);
    std::ifstream input(file, std::ios::binary);
    CHECK(input, "cannot open texture binary: {}", file.string());

    TextureHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    CHECK(input && header.magic == textureMagic && header.version == textureVersion,
          "invalid texture binary header: {}",
          file.string());

    CHECK(header.layout == static_cast<uint32_t>(desc.layout) &&
          header.format == static_cast<uint32_t>(desc.format) &&
          header.width == desc.width && header.height == desc.height &&
          header.layerCount == layerCount(desc.layout),
          "texture binary metadata disagrees with descriptor: {}",
          file.string());
    CHECK(std::filesystem::file_size(file) == sizeof(TextureHeader) + header.payloadBytes,
          "invalid texture binary size: {}",
          file.string());

    std::vector<uint8_t> bytes(header.payloadBytes);
    validateTexture(desc, bytes, file);

    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    CHECK(input, "failed reading texture binary: {}", file.string());

    return bytes;
}
