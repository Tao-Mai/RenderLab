#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "asset/texture_importer.h"

#include "asset/asset_desc.h"
#include "asset/asset_manager.h"
#include "asset/texture_io.h"
#include "core/config_manager.h"
#include "core/context.h"
#include "core/logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/gtc/packing.hpp>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <tinyexr.h>

namespace
{
struct StbiDeleter
{
    void operator()(stbi_uc* pixels) const { stbi_image_free(pixels); }
};

struct ExrPixelsDeleter
{
    void operator()(float* pixels) const { std::free(pixels); }
};

struct ExrHeaderDeleter
{
    void operator()(EXRHeader* header) const { FreeEXRHeader(header); }
};

struct ExrErrorDeleter
{
    void operator()(const char* message) const { FreeEXRErrorMessage(message); }
};

[[nodiscard]] std::filesystem::path sourceRelativeToRoot(
    const std::filesystem::path& source)
{
    const auto root = context().config->paths().assets;
    const auto relative = std::filesystem::relative(std::filesystem::absolute(source), root);
    CHECK(!relative.empty() && *relative.begin() != "..",
        "texture source is outside asset root: {}", source.string());
    return relative;
}

[[nodiscard]] std::filesystem::path binaryPath(const AssetId& id)
{
    CHECK(!id.empty() && id != TextureDesc::white &&
            std::filesystem::path{id}.filename() == std::filesystem::path{id} &&
            id != "." && id != "..",
        "invalid imported texture id: {}", id);
    return std::filesystem::path{"binary/texture"} / (id + ".bin");
}

[[nodiscard]] std::vector<uint8_t> loadImage(
    const std::filesystem::path& source, TextureDesc& desc)
{
    int width = 0;
    int height = 0;
    int channels = 0;
    const std::string filename = source.string();
    std::unique_ptr<stbi_uc, StbiDeleter> decoded(
        stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha));
    CHECK(decoded && width > 0 && height > 0,
        "failed to import texture '{}': {}", filename, stbi_failure_reason());
    const uint64_t count = static_cast<uint64_t>(width) * height * 4;
    CHECK(count <= std::numeric_limits<uint32_t>::max(),
        "texture is too large: {}", filename);
    desc.format = TextureDesc::DataFormat::Rgba8Srgb;
    desc.layout = TextureDesc::Layout::Image2D;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    return std::vector<uint8_t>(decoded.get(), decoded.get() + count);
}

[[nodiscard]] bool isColorChannel(const char* name)
{
    const std::string_view channel{name};
    return channel == "R" || channel == "G" || channel == "B" ||
        channel.ends_with(".R") || channel.ends_with(".G") ||
        channel.ends_with(".B");
}

[[nodiscard]] bool isExr(const std::filesystem::path& source)
{
    std::string extension = source.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".exr";
}

[[nodiscard]] glm::vec3 faceDirection(uint32_t face, float u, float v)
{
    switch (face)
    {
    case 0: return glm::normalize(glm::vec3{1.0f, -v, -u});
    case 1: return glm::normalize(glm::vec3{-1.0f, -v, u});
    case 2: return glm::normalize(glm::vec3{u, 1.0f, v});
    case 3: return glm::normalize(glm::vec3{u, -1.0f, -v});
    case 4: return glm::normalize(glm::vec3{u, -v, 1.0f});
    default: return glm::normalize(glm::vec3{-u, -v, -1.0f});
    }
}

[[nodiscard]] std::array<float, 4> sampleEquirectangular(
    const float* image, uint32_t width, uint32_t height, const glm::vec3& direction)
{
    const float longitude = std::atan2(direction.x, direction.z);
    const float latitude = std::acos(std::clamp(direction.y, -1.0f, 1.0f));
    const float x = (0.5f - longitude / (2.0f * std::numbers::pi_v<float>)) *
        static_cast<float>(width - 1);
    const float y = std::clamp(latitude / std::numbers::pi_v<float> *
        static_cast<float>(height - 1),
        0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, static_cast<int>(height) - 1);
    const int x1 = x0 + 1;
    const int y1 = std::clamp(y0 + 1, 0, static_cast<int>(height) - 1);
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);
    const auto wrapX = [width](int value) {
        const int size = static_cast<int>(width);
        return static_cast<uint32_t>((value % size + size) % size);
    };
    const auto texel = [image, width](uint32_t px, int py, uint32_t channel) {
        return image[(static_cast<size_t>(py) * width + px) * 4 + channel];
    };
    std::array<float, 4> result{};
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        const float upper = std::lerp(texel(wrapX(x0), y0, channel),
            texel(wrapX(x1), y0, channel), tx);
        const float lower = std::lerp(texel(wrapX(x0), y1, channel),
            texel(wrapX(x1), y1, channel), tx);
        result[channel] = std::lerp(upper, lower, ty);
    }
    return result;
}

enum class ExrProjection
{
    LatLong,
    Cube,
};

[[nodiscard]] std::optional<ExrProjection> readExrProjection(
    const EXRHeader& header, const std::string& filename)
{
    for (int index = 0; index < header.num_custom_attributes; ++index)
    {
        const EXRAttribute& attribute = header.custom_attributes[index];
        if (std::string_view{attribute.name} != "envmap")
        {
            continue;
        }
        CHECK(std::string_view{attribute.type} == "envmap" &&
                attribute.size == 1 && attribute.value != nullptr,
            "invalid EXR envmap attribute: {}", filename);
        if (attribute.value[0] == 0)
        {
            return ExrProjection::LatLong;
        }
        CHECK(attribute.value[0] == 1,
            "unknown EXR envmap layout in '{}'", filename);
        return ExrProjection::Cube;
    }
    return std::nullopt;
}

[[nodiscard]] std::array<float, 4> sampleOpenExrCube(
    const float* image, uint32_t side, uint32_t face, uint32_t x, uint32_t y)
{
    // OpenEXR stacks +X, -X, +Y, -Y, +Z, -Z vertically. Its face axes
    // differ from the Vulkan cube image axes.
    const bool flipVertical = face == 2 || face == 3;
    const uint32_t sourceX = flipVertical ? x : side - 1 - x;
    const uint32_t sourceY = flipVertical ? side - 1 - y : y;
    const float* source = image +
        ((static_cast<size_t>(face) * side + sourceY) * side + sourceX) * 4;
    return {source[0], source[1], source[2], source[3]};
}

struct DecodedExr
{
    std::unique_ptr<float, ExrPixelsDeleter> image;
    uint32_t width = 0;
    uint32_t height = 0;
    TextureDesc::DataFormat format;
    std::optional<ExrProjection> projection;
};

[[nodiscard]] DecodedExr decodeExr(const std::filesystem::path& source)
{
    const std::string filename = source.string();
    EXRVersion version{};
    CHECK(ParseEXRVersionFromFile(&version, filename.c_str()) == TINYEXR_SUCCESS &&
            !version.multipart && !version.non_image,
        "unsupported EXR version or multipart image: {}", filename);
    EXRHeader header{};
    InitEXRHeader(&header);
    std::unique_ptr<EXRHeader, ExrHeaderDeleter> headerGuard(&header);
    const char* headerError = nullptr;
    const int headerResult = ParseEXRHeaderFromFile(
        &header, &version, filename.c_str(), &headerError);
    std::unique_ptr<const char, ExrErrorDeleter> headerErrorGuard(headerError);
    CHECK(headerResult == TINYEXR_SUCCESS,
        "failed to read EXR header '{}': {}", filename,
        headerError ? headerError : "unknown error");

    int colorChannels = 0;
    bool hasFloat = false;
    for (int index = 0; index < header.num_channels; ++index)
    {
        const EXRChannelInfo& channel = header.channels[index];
        if (!isColorChannel(channel.name))
        {
            continue;
        }
        CHECK(channel.pixel_type == TINYEXR_PIXELTYPE_HALF ||
                channel.pixel_type == TINYEXR_PIXELTYPE_FLOAT,
            "unsupported EXR color channel type in '{}'", filename);
        hasFloat |= channel.pixel_type == TINYEXR_PIXELTYPE_FLOAT;
        ++colorChannels;
    }
    CHECK(colorChannels >= 3, "EXR has no RGB channels: {}", filename);
    const auto projection = readExrProjection(header, filename);

    float* raw = nullptr;
    int width = 0;
    int height = 0;
    const char* decodeError = nullptr;
    const int decoded = LoadEXR(&raw, &width, &height, filename.c_str(), &decodeError);
    std::unique_ptr<float, ExrPixelsDeleter> image(raw);
    std::unique_ptr<const char, ExrErrorDeleter> decodeErrorGuard(decodeError);
    CHECK(decoded == TINYEXR_SUCCESS && image && width > 0 && height > 0,
        "failed to decode EXR '{}': {}", filename,
        decodeError ? decodeError : "unknown error");
    return {
        .image = std::move(image),
        .width = static_cast<uint32_t>(width),
        .height = static_cast<uint32_t>(height),
        .format = hasFloat ? TextureDesc::DataFormat::Rgba32Float
                           : TextureDesc::DataFormat::Rgba16Float,
        .projection = projection,
    };
}

void writeExrPixel(
    uint8_t* output, const std::array<float, 4>& rgba,
    TextureDesc::DataFormat format)
{
    if (format == TextureDesc::DataFormat::Rgba16Float)
    {
        const std::array<uint16_t, 4> half{
            glm::packHalf1x16(rgba[0]), glm::packHalf1x16(rgba[1]),
            glm::packHalf1x16(rgba[2]), glm::packHalf1x16(rgba[3]),
        };
        std::memcpy(output, half.data(), sizeof(half));
    }
    else
    {
        std::memcpy(output, rgba.data(), sizeof(rgba));
    }
}

[[nodiscard]] std::vector<uint8_t> loadExrImage2D(
    const DecodedExr& decoded, TextureDesc& desc)
{
    desc.format = decoded.format;
    desc.layout = TextureDesc::Layout::Image2D;
    desc.width = decoded.width;
    desc.height = decoded.height;
    const uint64_t byteCount = static_cast<uint64_t>(decoded.width) *
        decoded.height * texture_io::bytesPerPixel(decoded.format);
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
        "imported EXR image is too large");
    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
    const size_t bytesPerPixel = texture_io::bytesPerPixel(decoded.format);
    for (size_t pixel = 0; pixel < static_cast<size_t>(decoded.width) * decoded.height; ++pixel)
    {
        const float* source = decoded.image.get() + pixel * 4;
        writeExrPixel(bytes.data() + pixel * bytesPerPixel,
            {source[0], source[1], source[2], source[3]}, decoded.format);
    }
    return bytes;
}

[[nodiscard]] std::vector<uint8_t> loadEnvironment(
    const DecodedExr& decoded, const std::filesystem::path& source,
    TextureDesc& desc)
{
    const std::string filename = source.string();
    const bool isLatLongShape = static_cast<uint64_t>(decoded.width) ==
        static_cast<uint64_t>(decoded.height) * 2;
    const bool isCubeShape = static_cast<uint64_t>(decoded.height) ==
        static_cast<uint64_t>(decoded.width) * 6;
    CHECK(decoded.projection.has_value() || isLatLongShape || isCubeShape,
        "EXR '{}' has no envmap attribute and is neither 2:1 nor 1:6", filename);
    const ExrProjection projection = decoded.projection.value_or(
        isCubeShape ? ExrProjection::Cube : ExrProjection::LatLong);
    CHECK((projection == ExrProjection::LatLong && isLatLongShape) ||
            (projection == ExrProjection::Cube && isCubeShape),
        "EXR '{}' dimensions disagree with its envmap layout", filename);
    const uint32_t side = projection == ExrProjection::Cube
        ? decoded.width : std::max(1u, decoded.height / 2);
    desc.format = decoded.format;
    desc.layout = TextureDesc::Layout::Cubemap;
    desc.width = side;
    desc.height = side;
    const uint64_t byteCount = static_cast<uint64_t>(side) * side * 6 *
        texture_io::bytesPerPixel(desc.format);
    CHECK(byteCount <= std::numeric_limits<uint32_t>::max(),
        "imported cubemap is too large: {}", filename);
    std::vector<uint8_t> bytes(static_cast<size_t>(byteCount));
    const size_t bytesPerPixel = texture_io::bytesPerPixel(desc.format);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t y = 0; y < side; ++y)
        {
            for (uint32_t x = 0; x < side; ++x)
            {
                const float u = 2.0f * (static_cast<float>(x) + 0.5f) / side - 1.0f;
                const float v = 2.0f * (static_cast<float>(y) + 0.5f) / side - 1.0f;
                const auto rgba = projection == ExrProjection::Cube
                    ? sampleOpenExrCube(decoded.image.get(), side, face, x, y)
                    : sampleEquirectangular(decoded.image.get(),
                        decoded.width, decoded.height,
                        faceDirection(face, u, v));
                uint8_t* output = bytes.data() +
                    ((static_cast<size_t>(face) * side + y) * side + x) * bytesPerPixel;
                writeExrPixel(output, rgba, desc.format);
            }
        }
    }
    LOG_INFO("imported EXR {}: {}x{}, {} cube faces, {}, {}",
        filename, decoded.width, decoded.height, side,
        projection == ExrProjection::Cube ? "CUBE" : "LATLONG",
        decoded.format == TextureDesc::DataFormat::Rgba32Float
            ? "RGBA32F" : "RGBA16F");
    return bytes;
}

[[nodiscard]] AssetId saveImported(
    AssetManager& assets, TextureDesc desc, const std::filesystem::path& source,
    std::span<const uint8_t> bytes)
{
    const auto binary = binaryPath(desc.id);
    const auto relativeSource = sourceRelativeToRoot(source);
    desc.path = relativeSource.generic_string();
    desc.binary = binary.generic_string();
    texture_io::write(assets.path(binary), desc, bytes);
    return assets.save(std::move(desc));
}
}

AssetId TextureImporter::importTexture(
    AssetManager& assets, AssetId id, const std::filesystem::path& source)
{
    TextureDesc desc{};
    desc.id = std::move(id);
    desc.source = "file";
    std::vector<uint8_t> bytes;
    if (isExr(source))
    {
        const DecodedExr decoded = decodeExr(source);
        if (decoded.projection.has_value())
        {
            desc.source = "environment";
            bytes = loadEnvironment(decoded, source, desc);
        }
        else
        {
            bytes = loadExrImage2D(decoded, desc);
        }
    }
    else
    {
        bytes = loadImage(source, desc);
    }
    return saveImported(assets, std::move(desc), source, bytes);
}

AssetId TextureImporter::importEnvironmentMap(
    AssetManager& assets, AssetId id, const std::filesystem::path& source)
{
    CHECK(isExr(source), "environment map must be EXR: {}", source.string());
    const DecodedExr decoded = decodeExr(source);
    TextureDesc desc{};
    desc.id = std::move(id);
    desc.source = "environment";
    const std::vector<uint8_t> bytes = loadEnvironment(decoded, source, desc);
    return saveImported(assets, std::move(desc), source, bytes);
}
