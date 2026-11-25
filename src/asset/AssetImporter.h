#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "asset/MeshAsset.h"
#include "asset/MeshGeometry.h"
#include "asset/TextureAsset.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <variant>
#include <vector>

class ModelImport;
struct Scene;

struct ImportTextureSetting
{
    std::filesystem::path     source;
    std::optional<ColorSpace> colorSpace;
};

struct ImportMeshSetting
{
    std::filesystem::path source;
    MeshAsset::ID         id;
    float                 importScale = 1.0f;
};

struct ImportSceneSetting
{
    std::filesystem::path source;
    AssetID<Scene>   id;
    float                 importScale = 1.0f;
};

struct ImportEnvironmentMapSetting
{
    std::filesystem::path source;
    // 源图与 radiance 的色彩空间；未指定时按文件格式推断。
    std::optional<ColorSpace> colorSpace;
};

class AssetImporter
{
public:
    struct PreparedMesh
    {
        struct Submesh
        {
            uint32_t firstIndex;
            uint32_t indexCount;
            int      materialIndex;
        };

        std::filesystem::path        source;
        MeshGeometry                 geometry;
        std::vector<Submesh>         submeshes;
        glm::vec3                    boundsMin{0.0f};
        glm::vec3                    boundsMax{0.0f};
        std::shared_ptr<ModelImport> importer;
        std::optional<uint32_t>      meshIndex;
    };

    struct PreparedEnvironmentMap
    {
        std::filesystem::path source;
        TextureAsset          radiance;
        std::vector<uint8_t> radianceBytes;
    };

    static TextureAsset::ID importTexture(const ImportTextureSetting& setting);

    static EnvironmentMapAsset::ID importEnvironmentMap(const ImportEnvironmentMapSetting& setting);
    [[nodiscard]] static std::optional<PreparedEnvironmentMap>
    prepareEnvironmentMap(const ImportEnvironmentMapSetting& setting, std::stop_token stop = {});
    static EnvironmentMapAsset::ID saveEnvironmentMap(PreparedEnvironmentMap&& prepared);

    static MeshAsset::ID             importMesh(const ImportMeshSetting& setting);
    [[nodiscard]] static PreparedMesh
    prepareMesh(const std::filesystem::path& source, std::optional<uint32_t> meshIndex = std::nullopt);
    static MeshAsset::ID       saveMesh(PreparedMesh&& prepared, const ImportMeshSetting& setting);
    static AssetID<Scene> importScene(const ImportSceneSetting& setting);

    struct LoadedImage
    {
        enum class Projection
        {
            LatLong,
            Cube,
        };

        enum class FileFormat
        {
            Exr,
            Hdr,
            JPEG,  // Other 8-bit formats decoded by stb use the same color-space default.
            Integer16,
        };

        ImageFormat                                            format;
        FileFormat                                             fileFormat;
        uint32_t                                               width  = 0;
        uint32_t                                               height = 0;
        std::optional<Projection>                              projection;
        std::optional<RgbChromaticities>                        chromaticities;
        std::variant<std::vector<uint8_t>, std::vector<float>> pixels;
    };

    [[nodiscard]] static LoadedImage loadImage(const std::filesystem::path& source);

private:
    static TextureAsset::ID importDdsTexture(const ImportTextureSetting& setting);

    struct CubemapPixels
    {
        std::vector<float>   linear;
        std::vector<uint8_t> encoded;
    };

    [[nodiscard]] static ColorSpace  defaultColorSpace(LoadedImage::FileFormat format);
    [[nodiscard]] static LoadedImage::Projection
    environmentProjection(const LoadedImage& image, const std::filesystem::path& source);
    [[nodiscard]] static CubemapPixels environmentCubemap(
        const LoadedImage&           image,
        LoadedImage::Projection      projection,
        uint32_t                     side,
        ImageFormat                  format,
        ColorSpace                   colorSpace,
        const std::filesystem::path& source
    );
    static void buildMipmapChain(CubemapPixels& cubemap, const TextureAsset& radiance, std::stop_token stop = {});
    [[nodiscard]] static TextureAsset::ID
    saveTexture(TextureAsset asset, const std::filesystem::path& source, std::span<const uint8_t> bytes);
};
