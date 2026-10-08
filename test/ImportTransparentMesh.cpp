// One-time import and short rendering check using the configured default scene.
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"

#include <charconv>
#include <cmath>
#include <iostream>
#include <string_view>

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 3);
    const std::string_view scaleArgument = argv[2];
    float                  importScale   = 0.0f;
    const auto parsed = std::from_chars(scaleArgument.data(), scaleArgument.data() + scaleArgument.size(), importScale);
    CHECK(parsed.ec == std::errc{} && parsed.ptr == scaleArgument.data() + scaleArgument.size());
    CHECK(std::isfinite(importScale) && importScale > 0.0f);

    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    Window           window;
    Renderer         renderer;
    auto&            ctx = context();
    ctx.config           = &config;
    ctx.assetManager     = &assets;
    ctx.assetDataManager = &data;
    ctx.sceneManager     = &scenes;
    ctx.window           = &window;
    ctx.renderer         = &renderer;

    config.init();
    data.init();
    scenes.init();
    assets.init();
    const auto  meshId   = AssetImporter::importMesh({.source = argv[1], .importScale = importScale});
    const auto& mesh     = assets.get<MeshAsset>(meshId);
    const auto  geometry = data.readGeometry(mesh);
    CHECK(!geometry.vertices.empty() && !geometry.indices.empty());

    MaterialAsset::ID transparentMaterial;
    for (const auto& submesh : mesh.submeshes)
    {
        const auto& material = assets.get<MaterialAsset>(submesh.materialId);
        if (material.alphaMode != "BLEND")
            continue;
        CHECK(material.baseColorFactor && material.baseColorFactor->a > 0.0f && material.baseColorFactor->a < 1.0f);
        transparentMaterial = material.id;
    }
    CHECK(!transparentMaterial.empty());

    scenes.loadDefaultScene();
    window.init();
    glfwHideWindow(window.nativeHandle());
    renderer.init();
    renderer.loadScene();
    const auto* importedMaterial            = &renderContext().resources->material(transparentMaterial);
    const auto  extent                      = renderer.swapchainHandle().extent();
    bool        renderedTransparentMaterial = false;
    for (int frame = 0; frame < 4; ++frame)
    {
        window.pollEvents();
        renderer.render(
            scenes.editorCamera(),
            {
                .viewport    = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height)},
                .aspectRatio = static_cast<float>(extent.width) / extent.height,
            }
        );
        for (const auto& item : renderer.renderData().transparentDrawItems)
            renderedTransparentMaterial |= item.material == importedMaterial;
    }
    CHECK(renderedTransparentMaterial);

    renderer.shutdown();
    window.shutdown();
    assets.shutdown();
    scenes.shutdown();
    data.shutdown();
    config.shutdown();
    ctx = {};
    std::cout << "Imported and rendered transparent mesh '" << meshId.value << "'.\n";
}
