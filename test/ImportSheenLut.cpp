// One-time import of the official 16-bit data LUT, without changing renderer/configuration.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "scene/SceneManager.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <stb_image.h>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 3, "usage: ImportSheenLut <source.png> <assets directory>");
    const auto source     = std::filesystem::absolute(argv[1]).lexically_normal();
    const auto assetsRoot = std::filesystem::absolute(argv[2]).lexically_normal();
    CHECK(stbi_is_16_bit(source.string().c_str()), "expected a 16-bit LUT: {}", source.string());

    int                                                        width    = 0;
    int                                                        height   = 0;
    int                                                        channels = 0;
    const std::unique_ptr<stbi_us, decltype(&stbi_image_free)> decoded{
        stbi_load_16(source.string().c_str(), &width, &height, &channels, 4),
        stbi_image_free
    };
    CHECK(
        decoded && width > 0 && height > 0 && channels == 3,
        "cannot decode RGB16 LUT '{}': {}",
        source.string(),
        stbi_failure_reason()
    );

    // Decode UNORM16 directly to float: no gamma conversion, row flip, or 8-bit intermediate.
    std::vector<float> pixels(static_cast<size_t>(width) * height * 4);
    std::ranges::transform(
        std::span{decoded.get(), pixels.size()},
        pixels.begin(),
        [](stbi_us value) { return static_cast<float>(value) / std::numeric_limits<stbi_us>::max(); }
    );
    std::vector<uint8_t> payload(pixels.size() * sizeof(float));
    std::memcpy(payload.data(), pixels.data(), payload.size());

    SceneManager     scenes;
    AssetManager     assets;
    AssetDataManager data;
    scenes.init();
    assets.init(assetsRoot);
    data.init(assetsRoot);

    TextureAsset texture;
    texture.id = TextureAsset::ID{source.stem().string()};
    CHECK(!assets.find<TextureAsset>(texture.id), "texture already exists: {}", texture.id);
    CHECK(
        !std::filesystem::exists(assetsRoot / "binary/texture" / (texture.id.value + ".bin")),
        "texture binary already exists: {}",
        texture.id
    );
    texture.source     = Source::File;
    texture.format     = ImageFormat::RGBA32F;
    texture.colorSpace = ColorSpace::Linear;
    texture.layout     = ImageLayout::Image2D;
    texture.width      = static_cast<uint32_t>(width);
    texture.height     = static_cast<uint32_t>(height);
    texture.mipLevels  = 1;  // Parameter-space lookup; do not average neighboring parameter values into mips.
    texture.path       = std::filesystem::relative(source, assetsRoot);
    texture.binary     = data.writeTexture(texture, payload);
    const auto id      = assets.save(texture);

    const auto persisted = asset_json::load<TextureAsset>(assetsRoot / TextureAsset::dir / (id.value + ".json"));
    CHECK(data.readTexture(persisted) == payload, "texture payload changed after persistence: {}", id);
    float minimum = 1.0f;
    float maximum = 0.0f;
    for (size_t pixel = 0; pixel < pixels.size(); pixel += 4)
    {
        minimum = std::min(minimum, pixels[pixel]);
        maximum = std::max(maximum, pixels[pixel]);
    }
    CHECK(minimum < maximum);
    LOG_INFO("Imported '{}': {}x{}, linear RGBA32F, 1 mip, red range [{}, {}]", id, width, height, minimum, maximum);

    data.shutdown();
    assets.shutdown();
    scenes.shutdown();
}
