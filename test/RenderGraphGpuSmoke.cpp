#include "Camera.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetDescManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "ecs/Light.h"
#include "ecs/Render.h"
#include "ecs/Transform.h"
#include "render/Renderer.h"

#include <algorithm>
#include <iostream>

// Explicit GPU smoke test: requires the configured assets, Vulkan and a window
// system. No asset or scene edits are saved; all changes are to this local scene.
int main(int argc, char** argv)
{
    logger::init(argv[0]);
    ConfigManager config;
    AssetDescManager assets;
    AssetDataManager data;
    Window window;
    Scene::Desc scene;
    Camera camera;
    Renderer renderer;
    Context& ctx = context();
    ctx.config = &config;
    ctx.assetDescManager = &assets;
    ctx.assetDataManager = &data;
    ctx.window = &window;
    ctx.scene = &scene;
    ctx.camera = &camera;
    ctx.renderer = &renderer;

    config.init();
    data.init();
    assets.init();
    window.init();
    glfwHideWindow(window.nativeHandle());
    scene = assets.desc<Scene>(config.initialScene());
    std::erase_if(scene.objects, [](const auto& object)
    {
        const auto render = object.components.find("Render");
        return render != object.components.end() &&
            render->second.try_cast<ecs::Render>()->meshId != Mesh::ID{"sphere"};
    });

    glm::vec3 center{};
    for (auto& object : scene.objects)
    {
        if (object.components.contains("Render"))
            center = object.components.at("Transform").try_cast<ecs::Transform>()->position;
        if (object.components.contains("Light"))
        {
            auto& light = *object.components.at("Light").try_cast<ecs::Light>();
            light.enabled = true;
            light.castShadow = true;
            light.range = 16.0f;
        }
    }
    scene.camera.position = center + glm::vec3{0.0f, 0.0f, 6.0f};
    scene.camera.yaw = -90.0f;
    scene.camera.pitch = 0.0f;
    camera.configure(scene.camera);
    renderer.init();
    renderer.loadScene(scene);

    const auto renderFrames = [&]
    {
        CHECK(renderer.gpuScene().renderItems().size() == 1, "smoke test requires one sphere");
        const uint32_t expectedId = renderer.gpuScene().renderItems().front().selectionId;
        const auto extent = renderer.swapchainHandle().extent();
        for (uint32_t frame = 0; frame < 8; ++frame)
        {
            window.pollEvents();
            auto& render = *renderer.gpuScene().renderItems().front().object->components.at("Render")
                .try_cast<ecs::Render>();
            if (frame == 2) render.materialOverrides[0].roughness = 0.65f;
            if (frame == 4) render.materialOverrides[0].baseColorTexture = TextureBinding{
                config.rendererConfig().brdfLut.textureID, BuiltinAssets::Sampler::linearClamp};
            if (frame == 6) render.materialOverrides.clear();

            for (size_t index = 1; index < renderer.gpuScene().lightRenderItems().size(); ++index)
            {
                auto& light = *renderer.gpuScene().lightRenderItems()[index].object->components.at("Light")
                    .try_cast<ecs::Light>();
                light.enabled = frame % 2 == 0;
            }

            const EditorFrameInput input{
                .viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height)},
                .aspectRatio = static_cast<float>(extent.width) / extent.height,
                .requestPick = (frame % 3 != 0),
                .pickX = extent.width / 2,
                .pickY = extent.height / 2,
            };
            const auto result = renderer.render(camera, input);
            CHECK(result.hasPickResult == input.requestPick, "missing picking result");
            if (input.requestPick)
                CHECK(result.pickedSelectionId == expectedId,
                    "expected sphere ID {}, got {}", expectedId, result.pickedSelectionId);
        }
    };
    renderFrames();

    // Grow both frames' SSBOs; place markers outside the picking ray.
    renderer.waitIdle();
    const auto lightTemplate = std::ranges::find_if(scene.objects,
        [](const auto& object) { return object.components.contains("Light"); });
    CHECK(lightTemplate != scene.objects.end(), "smoke test requires a light template");
    const Scene::Desc::Object templateObject = *lightTemplate;
    for (uint32_t index = 0; index < 32; ++index)
    {
        auto object = templateObject;
        object.name = "smoke-light-" + std::to_string(index);
        auto& light = *object.components.at("Light").try_cast<ecs::Light>();
        light.castShadow = false;
        light.intensity = 0.25f;
        object.components.at("Transform").try_cast<ecs::Transform>()->position =
            center + glm::vec3{20.0f + index, 20.0f, 0.0f};
        scene.objects.push_back(std::move(object));
    }
    renderer.loadScene(scene);
    renderFrames();

    // Rebuild all graph allocations and exercise shadow clear + shared fallback
    // texture imports on both frame contexts after real shadow drawing.
    scene.environment.environmentMap.reset();
    for (auto& object : scene.objects)
    {
        if (object.components.contains("Light"))
            object.components.at("Light").try_cast<ecs::Light>()->castShadow = false;
    }
    renderer.loadScene(scene);
    renderFrames();
    // A zero-light scene retains valid descriptors and skips SSBO reads.
    renderer.waitIdle();
    std::erase_if(scene.objects, [](const auto& object) { return object.components.contains("Light"); });
    renderer.loadScene(scene);
    renderFrames();
    renderer.waitIdle();
    renderer.shutdown();
    assets.shutdown();
    window.shutdown();
    config.shutdown();
    ctx = {};
    std::cout << "GPU smoke passed: shadow draws/clear, skybox/fallback, repeated picking, material/texture updates, multiple/disabled/zero lights, per-frame SSBO growth, graph rebuild, shutdown.\n";
}
