#include "editor/EditorCamera.h"
#include "editor/LightMarkers.h"
// One-time import and short rendering check using the configured default scene.
#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/resources/MaterialTable.h"
#include "scene/SceneManager.h"
#include "scene/system/RenderSystem.h"

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
    const auto  meshId   = AssetImporter::importMesh({.source = argv[1], .importScale = importScale});
    const auto& mesh     = assets.get<MeshAsset>(meshId);
    const auto  geometry = data.readGeometry(mesh);
    CHECK(!geometry.vertices.empty() && !geometry.indices.empty());

    MaterialAsset::ID transparentMaterial;
    for (const auto& submesh : mesh.submeshes)
    {
        const auto& material = assets.get<MaterialAsset>(submesh.materialId).as<GltfMaterial>();
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
    renderer.rebuildGraph();
    const auto extent                      = renderer.swapchainHandle().extent();
    bool       renderedTransparentMaterial = false;
    for (int frame = 0; frame < 4; ++frame)
    {
        window.pollEvents();
        const auto view =
            scenesCamera.renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
        const auto& renderData = renderSystem.prepare(scenes.scene());
        lightMarkers.prepare(scenes.scene(), *renderContext().resources);
        renderer.render(
            view,
            renderData,
            {
                .lightDrawItems = lightMarkers.drawItems(),
            }
        );
        renderer.waitIdle();
        const auto& gpuData = renderer.prepare(view, renderData, 0);
        for (uint32_t itemIndex = 0; itemIndex < gpuData.drawItems.size(); ++itemIndex)
        {
            if (!gpuData.isVisible(itemIndex))
                continue;
            const auto& item = gpuData.drawItems[itemIndex];
            renderedTransparentMaterial |= item.materialId == transparentMaterial &&
                renderContext().materialTable->info(gpuData.materialIndices[itemIndex]).alphaMode == kAlphaModeBlend;
        }
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
