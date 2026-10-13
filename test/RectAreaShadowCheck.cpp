#include "editor/EditorCamera.h"
// Standalone, short GPU check. Uses test configuration and never saves a scene.
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
#include "scene/component/RectAreaLightComponent.h"
#include "scene/component/RenderComponent.h"
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
    config.setRenderFeature(feature);
    config.setRenderPath(RenderPath::Rasterization);
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    window.init();
    glfwHideWindow(window.nativeHandle());

    SceneAsset scene;
    scene.id = "rect-shadow-check";
    scene.actors.push_back(std::make_unique<AStaticMesh>());
    auto actor                  = std::make_unique<ALight>(std::make_unique<RectAreaLightComponent>());
    actor->transform().position = {0.0f, 0.0f, 4.0f};
    actor->transform().rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    auto& light                 = *actor->getComponent<RectAreaLightComponent>();
    light.castShadow            = false;
    light.areaSize              = {2.0f, 1.0f};
    light.range                 = 16.0f;
    scene.actors.push_back(std::move(actor));
    scenes.load(scene);
    scenesCamera.config().position = {0.0f, 0.0f, 6.0f};
    renderer.init();
    renderer.rebuildGraph();

    DescriptorManager descriptors;
    descriptors.init(renderer.vulkanContext().deviceHandle());
    FrameData upload;
    upload.init(renderer.vulkanContext(), descriptors);
    LightData source;
    source.positionRange = {0.0f, 0.0f, 4.0f, 16.0f};
    source.direction     = {0.0f, 0.0f, -1.0f, 0.0f};
    source.areaRightData = {0.0f, 1.0f, 0.0f, 0.0f};
    source.flags         = {static_cast<uint32_t>(GpuLightType::RectArea), 1u, 1u, 0u};
    upload.update({}, {.lightCount = 1}, {&source, 1}, {.enable = true});
    CHECK(upload.rectAreaShadowData().size() == 1 && upload.lightData()[0].shadowidx == 0);
    CHECK(upload.pointShadowData().empty());
    const auto& shadow = upload.rectAreaShadowData()[0];
    CHECK(std::abs(shadow.nearPlane - 0.5f) < 1e-5f && shadow.farPlane == 16.0f);
    CHECK(glm::length(shadow.worldToUvScale - glm::vec2{0.41954982f}) < 1e-5f);
    const auto clip = upload.rectAreaShadowData()[0].vp * glm::vec4{0.0f, 0.0f, 3.0f, 1.0f};
    CHECK(std::abs(clip.x) < 1e-5f && std::abs(clip.y) < 1e-5f && clip.z > 0.0f && clip.z < clip.w);
    upload.update({}, {.lightCount = 1}, {&source, 1}, {.enable = false});
    CHECK(upload.rectAreaShadowData().empty() && upload.lightData()[0].shadowidx == ~0u);
    upload.reset();
    descriptors.shutdown();

    auto&      sceneLight = *scenes.scene().actors.back()->getComponent<LightComponent>();
    const auto extent     = renderer.swapchainHandle().extent();
    for (int frame = 0; frame < 8; ++frame)
    {
        if (frame == 2)
            sceneLight.castShadow = true;
        if (frame == 4)
        {
            feature.shadow.enable = false;
            config.setRenderFeature(feature);
            renderer.rebuildGraph();
        }
        if (frame == 6)
        {
            feature.shadow.enable = true;
            config.setRenderFeature(feature);
            sceneLight.castShadow = false;
            renderer.rebuildGraph();
        }

        window.pollEvents();
        const auto view = scenesCamera.renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
        const auto& renderData = renderSystem.prepare(scenes.scene(), view.uniforms, config.rendererConfig().feature.shadow);
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
    std::cout << "Rect area light GPU frames passed: no shadow, shadow enabled, global shadow disable and re-enable.\n";
}
