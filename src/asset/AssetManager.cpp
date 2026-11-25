#include "asset/AssetManager.h"

#include "asset/TextureMip.h"
#include "core/Json.h"

#include <cmath>
#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <system_error>
#include <type_traits>
#include <utility>

namespace
{
constexpr char kGeometryDirectory[] = "binary/geometry";
constexpr char kTextureDirectory[] = "binary/texture";
constexpr uint32_t kGeometryMagic = 0x4853454du;
constexpr uint32_t kGeometryVersion = 2;
constexpr uint32_t kTextureMagic = 0x58455452u;  // RTEX
constexpr uint32_t kTextureVersion = 2;

struct GeometryHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t vertexCount;
    uint32_t indexCount;
};

struct TextureHeader
{
    uint32_t magic;
    uint32_t version;
    uint32_t layout;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t mipLevels;
    uint32_t layerCount;
    uint32_t payloadBytes;

    bool operator==(const TextureHeader&) const = default;
};

static_assert(sizeof(GeometryHeader) == 4 * sizeof(uint32_t));
static_assert(sizeof(TextureHeader) == 9 * sizeof(uint32_t));
static_assert(std::is_trivially_copyable_v<Vertex> && sizeof(Vertex) == 12 * sizeof(float));

TextureHeader textureHeader(const TextureAsset& asset, uint32_t payloadBytes)
{
    return {
        .magic = kTextureMagic,
        .version = kTextureVersion,
        .layout = static_cast<uint32_t>(asset.layout),
        .format = static_cast<uint32_t>(asset.format),
        .width = asset.width,
        .height = asset.height,
        .mipLevels = asset.mipLevels,
        .layerCount = asset.layout == ImageLayout::Cubemap ? 6u : 1u,
        .payloadBytes = payloadBytes,
    };
}

void writeBinary(const std::filesystem::path& file, std::initializer_list<std::span<const std::byte>> blocks)
{
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    CHECK(!error, "cannot create asset data directory '{}': {}", file.parent_path().string(), error.message());

    auto temporary = file;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary);
    for (const auto block : blocks)
        output.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    output.close();
    CHECK(output, "cannot write asset data: {}", temporary.string());

    std::filesystem::rename(temporary, file, error);
    CHECK(!error, "cannot publish asset data '{}': {}", file.string(), error.message());
}

template <AssetType T>
void loadAssetDirectory(
    const std::filesystem::path& directory, std::unordered_map<typename T::ID, std::unique_ptr<T>>& out
)
{
    if (!std::filesystem::exists(directory))
    {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        auto asset = std::make_unique<T>(asset_json::load<T>(entry.path()));
        CHECK(!asset->id.empty(), "asset has empty id: {}", entry.path().string());
        if constexpr (std::same_as<T, MeshAsset>)
            for (glm::length_t axis = 0; axis < 3; ++axis)
                CHECK(
                    std::isfinite(asset->bounds.min[axis]) && std::isfinite(asset->bounds.max[axis]) &&
                        asset->bounds.min[axis] <= asset->bounds.max[axis],
                    "invalid mesh bounds: {}",
                    asset->id
                );

        const typename T::ID id = asset->id;
        CHECK(out.emplace(id, std::move(asset)).second, "duplicate asset ID: {}", id);
    }
}
}  // namespace

AssetManager::~AssetManager() {}

void AssetManager::init(std::filesystem::path assetRoot)
{
    root = std::move(assetRoot);
    loadAll();
}

void AssetManager::shutdown() noexcept
{
    clear();
    root.clear();
}

void AssetManager::loadAll()
{
    clear();
    AssetTypes::forEach([&]<AssetType T> { loadAssetDirectory<T>(root / T::dir, assetMap<T>()); });
}

void AssetManager::clear()
{
    revisions = {};
    std::apply([](auto&... maps) { (maps.clear(), ...); }, assets);
}

template <AssetType T>
typename T::ID AssetManager::saveAsset(std::unique_ptr<T> asset)
{
    const typename T::ID id = asset->id;
    const auto file = root / T::dir / (id.value + ".json");
    asset_json::save(file, *asset);

    assetMap<T>().insert_or_assign(id, std::move(asset));
    auto& revisionsForType = std::get<AssetRevisions<T>>(revisions);
    revisionsForType.latest = ++nextRevision;
    revisionsForType.byId[id] = revisionsForType.latest;

    return id;
}

template TextureAsset::ID        AssetManager::saveAsset(std::unique_ptr<TextureAsset>);
template MeshAsset::ID           AssetManager::saveAsset(std::unique_ptr<MeshAsset>);
template GltfMaterial::ID        AssetManager::saveAsset(std::unique_ptr<GltfMaterial>);
template Scene::ID               AssetManager::saveAsset(std::unique_ptr<Scene>);
template EnvironmentMapAsset::ID AssetManager::saveAsset(std::unique_ptr<EnvironmentMapAsset>);
template SamplerAsset::ID        AssetManager::saveAsset(std::unique_ptr<SamplerAsset>);

std::filesystem::path AssetManager::binaryPath(const std::filesystem::path& relative) const
{
    CHECK(!relative.empty() && !relative.is_absolute(), "asset data path must be relative: {}", relative.string());

    return (root / relative).lexically_normal();
}

std::filesystem::path AssetManager::writeGeometry(const MeshAsset::ID& id, const MeshGeometry& mesh) const
{
    CHECK(std::in_range<uint32_t>(mesh.vertices.size()) && std::in_range<uint32_t>(mesh.indices.size()),
          "mesh geometry exceeds the binary format limits: {}", id);

    const auto relative = std::filesystem::path{kGeometryDirectory} / (id.value + ".bin");
    const GeometryHeader header{
        .magic = kGeometryMagic,
        .version = kGeometryVersion,
        .vertexCount = static_cast<uint32_t>(mesh.vertices.size()),
        .indexCount = static_cast<uint32_t>(mesh.indices.size()),
    };
    writeBinary(root / relative, {
        std::as_bytes(std::span{&header, 1}),
        std::as_bytes(std::span{mesh.vertices}),
        std::as_bytes(std::span{mesh.indices}),
    });

    return relative;
}

MeshGeometry AssetManager::readGeometry(const MeshAsset& asset) const
{
    const auto file = binaryPath(asset.geometry);
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const auto fileSize = input.tellg();
    input.seekg(0);

    GeometryHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    const auto vertexBytes = uint64_t{header.vertexCount} * sizeof(Vertex);
    const auto indexBytes = uint64_t{header.indexCount} * sizeof(uint32_t);
    CHECK(
        input && header.magic == kGeometryMagic && header.version == kGeometryVersion &&
            static_cast<uint64_t>(fileSize) == sizeof(header) + vertexBytes + indexBytes,
        "invalid mesh geometry: {}",
        file.string()
    );

    MeshGeometry geometry;
    geometry.vertices.resize(header.vertexCount);
    geometry.indices.resize(header.indexCount);
    input.read(reinterpret_cast<char*>(geometry.vertices.data()), static_cast<std::streamsize>(vertexBytes));
    input.read(reinterpret_cast<char*>(geometry.indices.data()), static_cast<std::streamsize>(indexBytes));
    CHECK(input, "failed reading mesh geometry: {}", file.string());

    return geometry;
}

std::filesystem::path AssetManager::writeTexture(const TextureAsset& asset, std::span<const uint8_t> bytes) const
{
    CHECK(std::in_range<uint32_t>(bytes.size()), "texture exceeds the binary format limit: {}", asset.id);

    const auto relative = std::filesystem::path{kTextureDirectory} / (asset.id.value + ".bin");
    const auto header = textureHeader(asset, static_cast<uint32_t>(bytes.size()));
    writeBinary(root / relative, {std::as_bytes(std::span{&header, 1}), std::as_bytes(bytes)});

    return relative;
}

std::vector<uint8_t> AssetManager::readTexture(const TextureAsset& asset) const
{
    if (asset.source == Source::Builtin)
    {
        const auto* bytes = BuiltinAssets::textureData(asset.id);
        CHECK(bytes, "unknown builtin texture: {}", asset.id);

        return *bytes;
    }

    CHECK(asset.binary, "texture '{}' has no binary path", asset.id);
    const auto file = binaryPath(*asset.binary);
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    const auto fileSize = input.tellg();
    input.seekg(0);

    TextureHeader header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    CHECK(
        input && header == textureHeader(asset, header.payloadBytes) &&
            static_cast<uint64_t>(fileSize) == sizeof(header) + uint64_t{header.payloadBytes},
        "invalid texture binary: {}",
        file.string()
    );

    CHECK(
        header.width > 0 && header.height > 0 && header.mipLevels > 0 &&
            header.mipLevels <= texture_mip::maxLevels(header.width, header.height),
        "invalid texture dimensions or mip count: {}",
        file.string()
    );
    const auto block = imageBlock(asset.format);
    const auto blocksHigh = texture_mip::blockCount(header.height, block.extent);
    const auto blocksWide = texture_mip::blockCount(header.width, block.extent);
    CHECK(
        blocksWide <= header.payloadBytes / blocksHigh / header.layerCount / block.bytes &&
            texture_mip::totalBytes(header.width, header.height, header.layerCount, block, header.mipLevels) ==
                header.payloadBytes,
        "invalid texture payload: {}",
        file.string()
    );

    std::vector<uint8_t> bytes(header.payloadBytes);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    CHECK(input, "failed reading texture binary: {}", file.string());

    return bytes;
}
