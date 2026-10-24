#include "editor/LightMarkers.h"
#include "editor/EditorCamera.h"
// Short manual check; uses temporary actors and never saves a scene.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/resource/RenderResourceManager.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"
#include "scene/system/RenderSystem.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

int main(int argc, char** argv)
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
    context().config           = &config;
    context().assetManager     = &assets;
    context().assetDataManager = &data;
    context().sceneManager     = &scenes;
    context().window           = &window;
    context().renderer         = &renderer;
    config.init();
    const auto originalPath    = config.rendererConfig().renderPath;
    const auto originalFeature = config.rendererConfig().feature;
    scenes.init();
    assets.init(config.paths().assets);
    data.init(config.paths().assets);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    SceneAsset scene;
    scene.id                    = "editor_overlay_check";
    scene.editorCamera.position = {0, 0, 5};
    scene.editorCamera.yaw      = -90;
    scene.editorCamera.pitch    = 0;
    scene.actors.push_back(std::make_unique<AStaticMesh>());
    scene.actors.push_back(std::make_unique<ALight>(std::make_unique<SpotLightComponent>()));
    scene.actors.back()->getComponent<LightComponent>()->enabled = false;
    scenes.load(scene);
    renderer.init();

    auto& markerActor   = *scenes.scene().actors.back();
    auto& markerLight   = *markerActor.getComponent<SpotLightComponent>();
    markerLight.enabled = true;
    lightMarkers.prepare(scenes.scene(), *renderContext().resources);
    CHECK(lightMarkers.drawItems().size() == 2);
    CHECK(lightMarkers.drawItems()[0].color == glm::vec3{0.28f});
    CHECK(lightMarkers.drawItems()[1].color == markerLight.emissionColor());
    const auto checkCone = [&]
    {
        lightMarkers.prepare(scenes.scene(), *renderContext().resources);
        const auto& marker = lightMarkers.drawItems().front();
        CHECK(marker.mesh == &renderContext().resources->mesh(BuiltinAssets::Mesh::kCone));
        CHECK(marker.selectionId == markerActor.selectionId());
        CHECK(lightMarkers.drawItems()[1].selectionId == markerActor.selectionId());
        const auto axis         = glm::normalize(glm::vec3{marker.model * glm::vec4{0, 0, -1, 0}});
        const auto edge         = glm::normalize(glm::vec3{marker.model * glm::vec4{1, 0, -1, 0}});
        const auto expectedAxis = markerActor.transform().rotation * glm::vec3{0, 0, -1};
        CHECK(glm::length(axis - expectedAxis) < 1e-5f);
        CHECK(std::abs(glm::dot(axis, edge) - markerLight.cosOuter) < 1e-5f);
    };
    checkCone();
    markerActor.transform().rotation = glm::angleAxis(glm::radians(60.0f), glm::vec3{0, 1, 0});
    markerLight.cosOuter             = std::cos(glm::radians(45.0f));
    checkCone();
    markerActor.transform().rotation = glm::quat{1, 0, 0, 0};
    markerLight.enabled              = false;
    lightMarkers.prepare(scenes.scene(), *renderContext().resources);
    CHECK(lightMarkers.drawItems()[0].color == glm::vec3{0.12f});
    CHECK(lightMarkers.drawItems()[1].color == markerLight.color * 0.15f);

    const auto extent = renderer.swapchainHandle().extent();
    const auto view =
        scenesCamera.renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
    EditorFrameInput input{
        .requestPick = true,
        .pickX       = extent.width / 2,
        .pickY       = extent.height / 2,
    };

    for (const auto path : {RenderPath::Forward, RenderPath::Deferred, RenderPath::RayTracing})
    {
        if (path == RenderPath::RayTracing && !renderer.vulkanContext().supportsRayTracing())
            continue;

        config.setRenderPath(path);
        for (const bool hdr : {false, true})
        {
            auto feature          = originalFeature;
            feature.hdr           = hdr;
            feature.ibl           = false;
            feature.shadow.enable = false;
            config.setRenderFeature(feature);
            for (const float z : {1.5f, -1.5f})
            {
                auto& light                = *scenes.scene().actors[1];
                light.transform().position = {0, 0, z};
                window.pollEvents();
                const auto& renderData = renderSystem.prepare(scenes.scene());
                lightMarkers.prepare(scenes.scene(), *renderContext().resources);
                input.lightDrawItems = lightMarkers.drawItems();
                CHECK(!input.lightDrawItems.empty());
                const auto result    = renderer.render(view, renderData, input);
                const auto expected  = z > 0 ? light.selectionId() : scenes.scene().actors[0]->selectionId();
                CHECK(
                    result.hasPickResult && result.pickedSelectionId == expected,
                    "path {} HDR {} light Z {}: picked {}, expected {}",
                    static_cast<int>(path),
                    hdr,
                    z,
                    result.pickedSelectionId,
                    expected
                );
            }
        }
        LOG_INFO("Spot cone marker visibility and picking passed for render path {}", static_cast<int>(path));
    }

    renderer.waitIdle();
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setRenderPath(originalPath);
    config.setRenderFeature(originalFeature);
    config.shutdown();
    context() = {};
}
