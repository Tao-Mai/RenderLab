#include "editor/EditorCamera.h"
#include "editor/LightMarkers.h"
// Short manual check, linked against the application's existing objects.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/DirectionalLightComponent.h"
#include "scene/system/RenderSystem.h"

#include <cmath>
#include <iostream>

int main(int, char** argv)
{
    logger::init(argv[0]);
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
    const auto original                  = config.rendererConfig();
    auto       feature                   = original.feature;
    feature.ibl                          = false;
    feature.shadow.enable                = true;
    feature.shadow.pointShadowMapSize    = 256;
    feature.shadow.rectAreaShadowMapSize = 256;
    feature.shadow.cascade               = {.count = 4, .shadowMapSize = 256, .maxDistance = 40.0f};
    config.setRenderFeature(feature);
    config.setRenderPath(RenderPath::Rasterization);
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    window.init();
    glfwHideWindow(window.nativeHandle());

    SceneAsset scene;
    scene.id                    = "directional-shadow-check";
    scene.editorCamera.position = {0.0f, 1.0f, 6.0f};
    scene.actors.push_back(std::make_unique<AStaticMesh>());
    auto  actor      = std::make_unique<ALight>(std::make_unique<DirectionalLightComponent>());
    auto& light      = *actor->getComponent<LightComponent>();
    light.castShadow = true;
    scene.actors.push_back(std::move(actor));
    scenes.load(scene);
    renderer.init();
    renderer.rebuildGraph();

    FrameData upload;
    upload.init(renderer.vulkanContext(), *renderContext().device);
    const auto&        camera         = scenesCamera;
    const auto         viewProjection = camera.projectionMatrix(1.0f) * camera.viewMatrix();
    const ViewUniforms view{
        .viewProjection        = viewProjection,
        .inverseViewProjection = glm::inverse(viewProjection),
        .cameraPosition        = glm::vec4{camera.worldPosition(), 1.0f},
        .view                  = camera.viewMatrix(),
    };
    RenderLight source;
    source.data.direction     = {0.0f, -1.0f, 0.0f, 0.0f};
    source.data.lightType     = GpuLightType::Directional;
    source.data.shadowEnabled = 1;
    auto prepared             = prepareShadows(view, {&source, 1}, feature.shadow);
    upload.update(
        view,
        {.lightRanges = prepared.lightRanges, .cascadeCount = feature.shadow.cascade.count},
        std::move(prepared)
    );
    CHECK(upload.directionalShadowData().size() == 1 && upload.lightData()[0].shadowidx == 0);
    CHECK(upload.lightUniforms().cascadeCount == 4);
    float previousDepth = camera.config().nearPlane;
    for (const auto& cascade : upload.directionalShadowData()[0].cascades)
    {
        CHECK(cascade.depthUpperBound > previousDepth);
        const auto worldCenter =
            camera.worldPosition() + camera.forward() * (previousDepth + cascade.depthUpperBound) * 0.5f;
        const auto clip = cascade.vp * glm::vec4{worldCenter, 1.0f};
        CHECK(std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= 0.0f && clip.z <= clip.w);
        previousDepth = cascade.depthUpperBound;
    }
    CHECK(std::abs(previousDepth - feature.shadow.cascade.maxDistance) < 0.001f);
    upload.reset();

    const auto extent = renderer.swapchainHandle().extent();
    for (int frame = 0; frame < 8; ++frame)
    {
        if (frame == 2)
        {
            feature.shadow.cascade.count = 1;
            config.setRenderFeature(feature);
            renderer.rebuildGraph();
        }
        if (frame == 4 || frame == 6)
        {
            feature.shadow.enable = frame == 6;
            config.setRenderFeature(feature);
            renderer.rebuildGraph();
        }
        window.pollEvents();
        const auto view =
            camera.renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
        const auto& renderData = renderSystem.prepare(scenes.scene());
        lightMarkers.prepare(scenes.scene(), *renderContext().resources);
        renderer.render(
            view,
            renderData,
            {
                .lightDrawItems = lightMarkers.drawItems(),
            }
        );
    }

    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setRenderPath(original.renderPath);
    config.setRenderFeature(original.feature);
    config.shutdown();
    ctx = {};
    std::cout << "Directional shadow upload and GPU frames passed (4/1 cascades, disable/re-enable).\n";
}
