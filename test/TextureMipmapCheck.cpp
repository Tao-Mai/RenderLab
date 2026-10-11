#include "editor/EditorCamera.h"
// Small filtering/upload check and one-time rebake of existing ordinary textures.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/TextureMip.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "scene/SceneManager.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

#include <glm/gtc/packing.hpp>

namespace
{
void checkFiltering()
{
    TextureAsset texture;
    texture.layout = ImageLayout::Image2D;
    texture.format = ImageFormat::RGB8;
    texture.width = texture.height = 2;
    texture.mipLevels              = 2;
    const std::array<uint8_t, 12> pixels{0, 0, 0, 255, 255, 255, 0, 0, 0, 255, 255, 255};
    texture.colorSpace = ColorSpace::Srgb;
    const auto srgb    = texture_mip::generateChain(texture, pixels);
    CHECK(std::equal(pixels.begin(), pixels.end(), srgb.begin()));
    CHECK(srgb.size() == 15 && srgb[12] >= 187 && srgb[12] <= 188);
    texture.colorSpace = ColorSpace::Linear;
    const auto linear  = texture_mip::generateChain(texture, pixels);
    CHECK(linear[12] >= 127 && linear[12] <= 128);

    texture.format     = ImageFormat::RGBA8;
    texture.colorSpace = ColorSpace::Srgb;
    const std::array<uint8_t, 16> alphaPixels{255, 0, 0, 255, 0, 0, 255, 0, 255, 0, 0, 255, 0, 0, 255, 0};
    const auto                    alpha = texture_mip::generateChain(texture, alphaPixels);
    CHECK(alpha[16] == 255 && alpha[17] == 0 && alpha[18] == 0 && alpha[19] >= 127 && alpha[19] <= 128);
}

void checkNpotUploads(VulkanContext& vulkan)
{
    auto                   pool = vkCheck(vulkan.deviceHandle().createCommandPool({
                          .queueFamilyIndex = vulkan.graphicsQueueFamilyIndex(),
    }));
    const GpuUploadContext upload{vulkan.physicalDeviceHandle(), vulkan.deviceHandle(), pool, vulkan.queueHandle()};
    for (const auto format :
         {ImageFormat::R8,
          ImageFormat::RG8,
          ImageFormat::RGB8,
          ImageFormat::RGBA8,
          ImageFormat::RGBA16F,
          ImageFormat::RGBA32F})
    {
        TextureAsset texture;
        texture.id                      = "mip-upload-check";
        texture.layout                  = ImageLayout::Image2D;
        texture.format                  = format;
        texture.colorSpace              = ColorSpace::Linear;
        texture.width                   = 3;
        texture.height                  = 5;
        texture.mipLevels               = texture_mip::maxLevels(texture.width, texture.height);
        const auto           pixelBytes = AssetDataManager::bytesPerPixel(format);
        std::vector<uint8_t> base(texture.width * texture.height * pixelBytes, 128);
        if (format == ImageFormat::RGBA16F || format == ImageFormat::RGBA32F)
        {
            const float    value        = 0.5f;
            const uint16_t half         = glm::packHalf1x16(value);
            const auto     channelBytes = pixelBytes / 4;
            for (size_t offset = 0; offset < base.size(); offset += channelBytes)
                std::memcpy(
                    base.data() + offset,
                    channelBytes == 2 ? static_cast<const void*>(&half) : &value,
                    channelBytes
                );
        }
        const auto       pixels = texture_mip::generateChain(texture, base);
        const GpuTexture gpu{upload, texture, pixels};
        CHECK(gpu.imageInfo().mipLevels == 3);
    }
}
}  // namespace

int main(int, char** argv)
{
    logger::init(argv[0]);
    checkFiltering();
    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    EditorCamera     scenesCamera{scenes.scene().editorCamera};
    Window           window;
    Renderer         renderer;
    RenderSystem     renderSystem;
    LightMarkers     lightMarkers;
    auto&            ctx = context();
    ctx.config           = &config;
    ctx.assetManager     = &assets;
    ctx.assetDataManager = &data;
    ctx.sceneManager     = &scenes;
    ctx.window           = &window;
    ctx.renderer         = &renderer;
    config.init();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);

    size_t rebuilt = 0;
    for (const auto& id : assets.loadedIds<TextureAsset>())
    {
        TextureAsset texture = assets.get<TextureAsset>(id);
        if (texture.source != Source::File || texture.layout != ImageLayout::Image2D ||
            id == config.rendererConfig().brdfLut.textureID ||
            texture.mipLevels == texture_mip::maxLevels(texture.width, texture.height))
            continue;
        const auto original  = data.readTexture(texture);
        const auto baseBytes = texture_mip::levelBytes(
            texture.width,
            texture.height,
            1,
            AssetDataManager::bytesPerPixel(texture.format),
            0
        );
        texture.mipLevels = texture_mip::maxLevels(texture.width, texture.height);
        const auto pixels = texture_mip::generateChain(texture, std::span{original}.first(baseBytes));
        texture.binary    = data.writeTexture(texture, pixels);
        assets.save(texture);
        CHECK(data.readTexture(assets.get<TextureAsset>(id)) == pixels);
        ++rebuilt;
    }

    scenes.loadDefaultScene();
    window.init();
    glfwHideWindow(window.nativeHandle());
    renderer.init();
    checkNpotUploads(renderer.vulkanContext());
    renderer.rebuildGraph();
    const auto extent = renderer.swapchainHandle().extent();
    for (int frame = 0; frame < 4; ++frame)
    {
        window.pollEvents();
        const auto& renderData = renderSystem.prepare(scenes.scene(), scenesCamera.worldPosition());
        lightMarkers.prepare(scenes.scene(), *renderContext().resources);
        renderer.render(
            scenesCamera.renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)}),
            renderData,
            {
                .lightDrawItems = lightMarkers.drawItems(),
            }
        );
    }
    renderer.shutdown();
    window.shutdown();
    assets.shutdown();
    scenes.shutdown();
    data.shutdown();
    config.shutdown();
    ctx = {};
    std::cout << "Rebuilt " << rebuilt << " textures; filtering, NPOT uploads and scene rendering passed.\n";
}
