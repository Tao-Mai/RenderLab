#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Serializer.h"
#include "core/Window.h"
#include "editor/Editor.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/FreeFlyMoveComponent.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <iostream>

namespace
{
void checkPointShadowUpload(const VulkanContext& vulkan)
{
    DescriptorManager descriptors;
    descriptors.init(vulkan.deviceHandle());
    FrameData frame;
    frame.init(vulkan, descriptors);
    std::array<LightData, 5> lights;
    for (auto& light : lights)
    {
        light.positionRange = {2.0f, 3.0f, 4.0f, 16.0f};
        light.flags         = {static_cast<uint32_t>(LightComponent::Type::Point), 1u, 1u, 0u};
    }
    lights[0].flags.y = 0;
    lights[1].flags.x = static_cast<uint32_t>(LightComponent::Type::Spot);
    lights[2].flags.z = 0;
    frame
        .update({}, {.lightCount = static_cast<uint32_t>(lights.size())}, prepareShadows({}, lights, {.enable = true}));
    CHECK(frame.pointShadowData().size() == 2);
    CHECK(frame.lightData()[3].shadowidx == 0 && frame.lightData()[4].shadowidx == 1);
    CHECK(
        frame.lightData()[0].shadowidx == ~0u && frame.lightData()[1].shadowidx == ~0u &&
        frame.lightData()[2].shadowidx == ~0u
    );

    const std::array directions = {
        glm::vec3{1, 0, 0},
        glm::vec3{-1, 0, 0},
        glm::vec3{0, 1, 0},
        glm::vec3{0, -1, 0},
        glm::vec3{0, 0, 1},
        glm::vec3{0, 0, -1},
    };
    for (size_t face = 0; face < directions.size(); ++face)
    {
        const glm::vec4 clip =
            frame.pointShadowData()[0].vp[face] * glm::vec4{lights[3].position() + directions[face], 1.0f};
        CHECK(
            std::abs(clip.x) < 1.0e-5f && std::abs(clip.y) < 1.0e-5f && clip.w > 0.0f,
            "point shadow VP must face its corresponding cubemap direction"
        );
    }

    lights[0].flags.y = 1;
    lights[2].flags.z = 1;
    frame
        .update({}, {.lightCount = static_cast<uint32_t>(lights.size())}, prepareShadows({}, lights, {.enable = true}));
    CHECK(frame.pointShadowData().size() == 4);
    CHECK(
        frame.lightData()[0].shadowidx == 0 && frame.lightData()[2].shadowidx == 1 &&
        frame.lightData()[3].shadowidx == 2 && frame.lightData()[4].shadowidx == 3
    );
    frame.update(
        {},
        {.lightCount = static_cast<uint32_t>(lights.size())},
        prepareShadows({}, lights, {.enable = false})
    );
    CHECK(frame.pointShadowData().empty());
    for (const auto& light : frame.lightData())
        CHECK(!light.castShadow() && light.shadowidx == ~0u);
    CHECK(lights[0].castShadow());

    frame.update({}, {}, prepareShadows({}, {}, {.enable = true}));
    CHECK(frame.pointShadowData().empty() && frame.lightData().empty());
    frame.reset();
    descriptors.shutdown();
}

void checkMovement(const SceneAsset& original, Window& window, SceneManager& manager)
{
    auto& input = *context().inputManager;

    // Drive installed callbacks without OS input or changing the saved scene.
    const auto callback = glfwSetKeyCallback(window.nativeHandle(), nullptr);
    CHECK(callback != nullptr, "movement test requires the Window key callback");
    glfwSetKeyCallback(window.nativeHandle(), callback);
    const auto mouseCallback = glfwSetMouseButtonCallback(window.nativeHandle(), nullptr);
    CHECK(mouseCallback != nullptr, "navigation test requires the mouse callback");
    glfwSetMouseButtonCallback(window.nativeHandle(), mouseCallback);
    const auto key = [&](int code, int action) { callback(window.nativeHandle(), code, 0, action, 0); };

    SceneAsset scene;
    Deserialize(Serialize(original), scene);
    const size_t freeFlyPeerIndex                           = scene.actors.size();
    auto         freeFlyPeer                                = std::make_unique<AStaticMesh>();
    freeFlyPeer->name                                       = "FreeFly peer";
    freeFlyPeer->addComponent<FreeFlyMoveComponent>().speed = 4.0f;
    scene.actors.push_back(std::move(freeFlyPeer));

    const size_t characterPeerIndex                             = scene.actors.size();
    auto         characterPeer                                  = std::make_unique<AStaticMesh>();
    characterPeer->name                                         = "Character peer";
    characterPeer->addComponent<CharacterMoveComponent>().speed = 1.0f;
    scene.actors.push_back(std::move(characterPeer));

    auto& cameraData     = scene.editorCamera;
    cameraData.yaw       = 0.0f;
    cameraData.pitch     = 45.0f;
    cameraData.speed     = 2.0f;
    scene.environment.up = {0.0f, 0.0f, 3.0f};
    manager.load(scene);
    context().editor->tick(0.0f);

    const auto camera       = [&]() -> EditorCamera& { return context().editor->camera(); };
    const auto position     = [&]() -> glm::vec3& { return context().editor->camera().config().position; };
    const auto peerPosition = [&](size_t index) -> glm::vec3&
    { return manager.scene().actors[index]->transform().position; };
    const auto expectPosition = [&](glm::vec3 expected)
    { CHECK(glm::length(position() - expected) < 1.0e-5f, "unexpected movement displacement"); };
    const auto activateFreeMovement = [&]
    {
        key(GLFW_KEY_V, GLFW_PRESS);
        input.tick();
        context().editor->tick(0.0f);
        CHECK(camera().isNavigationActive(), "V must activate free movement");
        key(GLFW_KEY_V, GLFW_RELEASE);
        input.tick();
    };

    // WASD is inactive without right mouse or V; right mouse navigates temporarily.
    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position() = {};
    context().editor->tick(0.1f);
    manager.tick(0.1f);
    CHECK(!camera().isNavigationActive(), "movement must require navigation");
    expectPosition({});
    mouseCallback(window.nativeHandle(), GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    input.tick();
    context().editor->tick(0.1f);
    manager.tick(0.1f);
    CHECK(camera().isNavigationActive(), "right mouse must enable camera movement");
    expectPosition(camera().forward() * 0.2f);
    mouseCallback(window.nativeHandle(), GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    key(GLFW_KEY_W, GLFW_RELEASE);
    input.tick();
    context().editor->tick(0.0f);
    manager.tick(0.0f);
    CHECK(!camera().isNavigationActive(), "releasing right mouse must stop navigation");

    activateFreeMovement();
    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position()                       = {};
    peerPosition(freeFlyPeerIndex)   = {};
    peerPosition(characterPeerIndex) = {};
    context().editor->tick(0.1f);
    manager.tick(0.1f);
    expectPosition(camera().forward() * 0.2f);
    CHECK(
        glm::length(peerPosition(freeFlyPeerIndex) - camera().forward() * 0.4f) < 1.0e-5f,
        "FreeFlyMove must tick each actor at its own speed"
    );
    CHECK(
        glm::length(peerPosition(characterPeerIndex) - camera().forward() * 0.1f) < 1.0e-5f,
        "CharacterMove must work on actors without Camera"
    );

    key(GLFW_KEY_W, GLFW_RELEASE);
    key(GLFW_KEY_E, GLFW_PRESS);
    input.tick();
    position() = {};
    context().editor->tick(0.1f);
    manager.tick(0.1f);
    expectPosition(camera().up() * 0.2f);
    key(GLFW_KEY_E, GLFW_RELEASE);

    cameraData.yaw       = -90.0f;
    cameraData.pitch     = 60.0f;
    scene.environment.up = {0.0f, 5.0f, 0.0f};
    manager.load(scene);
    activateFreeMovement();

    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position()                       = {};
    peerPosition(freeFlyPeerIndex)   = {};
    peerPosition(characterPeerIndex) = {};
    context().editor->tick(0.1f);
    manager.tick(0.1f);
    expectPosition(camera().forward() * 0.2f);
    CHECK(
        glm::length(peerPosition(characterPeerIndex) - glm::vec3{0.0f, 0.0f, -0.1f}) < 1.0e-5f,
        "CharacterMove must tick each actor at its own speed"
    );
    CHECK(
        glm::length(peerPosition(freeFlyPeerIndex) - camera().forward() * 0.4f) < 1.0e-5f,
        "FreeFlyMove must not be limited to the camera actor"
    );

    manager.scene().environment.up = {0.0f, 0.0f, 2.0f};
    position()                     = {};
    context().editor->tick(0.1f);
    manager.tick(0.1f);
    expectPosition(camera().forward() * 0.2f);

    manager.scene().environment.up   = camera().forward();
    position()                       = {};
    peerPosition(characterPeerIndex) = {};
    context().editor->tick(0.1f);
    manager.tick(0.1f);
    CHECK(
        std::isfinite(glm::length(peerPosition(characterPeerIndex))) &&
            std::abs(glm::dot(peerPosition(characterPeerIndex), manager.scene().environment.up)) < 1.0e-5f,
        "looking along Up must retain finite planar movement"
    );

    key(GLFW_KEY_W, GLFW_RELEASE);
    for (const int code : {GLFW_KEY_E, GLFW_KEY_Q})
    {
        key(code, GLFW_PRESS);
        input.tick();
        position()                       = {};
        peerPosition(characterPeerIndex) = {};
        context().editor->tick(0.1f);
        manager.tick(0.1f);
        const float sign = code == GLFW_KEY_E ? 1.0f : -1.0f;
        expectPosition(camera().up() * 0.2f * sign);
        CHECK(glm::length(peerPosition(characterPeerIndex)) == 0.0f, "CharacterMove must ignore E/Q");
        key(code, GLFW_RELEASE);
    }

    input.tick();
    manager.load(original);
    context().editor->tick(0.0f);
}
}  // namespace

// Explicit integration test requiring Vulkan and configured assets. No scene is saved.
int main(int argc, char** argv)
{
    logger::init(argv[0]);
    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    Window           window;
    SceneAsset       scene;
    SceneManager     sceneManager;
    EditorCamera     sceneManagerCamera{sceneManager.scene().editorCamera};
    Renderer         renderer;
    RenderSystem     renderSystem;
    LightMarkers     lightMarkers;
    Context&         ctx = context();
    ctx.config           = &config;
    ctx.assetManager     = &assets;
    ctx.assetDataManager = &data;
    ctx.window           = &window;
    ctx.sceneManager     = &sceneManager;
    ctx.renderer         = &renderer;

    config.init();
    const auto originalRenderer = config.rendererConfig();
    data.init(config.paths().assets);
    sceneManager.init();
    assets.init(config.paths().assets);
    const auto environmentIds = assets.loadedIds<EnvironmentMapAsset>();
    CHECK(!environmentIds.empty());
    window.init();
    glfwHideWindow(window.nativeHandle());
    Deserialize(Serialize(assets.get<SceneAsset>(config.initialScene())), scene);
    scene.environmentMap = environmentIds.front();
    std::erase_if(
        scene.actors,
        [](const auto& actor) { return actor->template getComponent<RenderComponent>() != nullptr; }
    );
    auto sphere                                     = std::make_unique<AStaticMesh>();
    sphere->getComponent<RenderComponent>()->meshId = BuiltinAssets::Mesh::sphere;
    scene.actors.push_back(std::move(sphere));

    glm::vec3 center{};
    for (const auto& actor : scene.actors)
    {
        if (actor->getComponent<RenderComponent>())
            center = actor->transform().position;
        if (auto* light = actor->getComponent<LightComponent>())
        {
            light->enabled    = true;
            light->castShadow = true;
            if (auto* local = dynamic_cast<LocalLightComponent*>(light))
                local->range = 16.0f;
        }
    }
    scene.editorCamera.yaw      = -90.0f;
    scene.editorCamera.pitch    = 0.0f;
    scene.editorCamera.position = center + glm::vec3{0.0f, 0.0f, 6.0f};

    sceneManager.load(scene);
    renderer.init();
    checkPointShadowUpload(renderer.vulkanContext());
    renderer.rebuildGraph();

    const auto renderActor = [&]() -> AActor*
    {
        for (const auto& actor : sceneManager.scene().actors)
            if (actor->getComponent<RenderComponent>())
                return actor.get();
        return nullptr;
    };
    const auto renderFrames = [&]
    {
        CHECK(
            std::ranges::count_if(
                sceneManager.scene().actors,
                [](const auto& actor) { return actor->template getComponent<RenderComponent>() != nullptr; }
            ) == 1,
            "smoke test requires one sphere"
        );
        const uint32_t               expectedId = renderActor()->selectionId();
        std::vector<LightComponent*> lights;
        for (const auto& actor : sceneManager.scene().actors)
            if (auto* light = actor->getComponent<LightComponent>())
                lights.push_back(light);
        const auto extent = renderer.swapchainHandle().extent();
        for (uint32_t frame = 0; frame < 8; ++frame)
        {
            window.pollEvents();
            auto& render = *renderActor()->getComponent<RenderComponent>();
            if (frame == 2)
                render.materialOverrides[0].roughness = 0.65f;
            if (frame == 4)
                render.materialOverrides[0].baseColorTexture =
                    TextureBinding{config.rendererConfig().brdfLut.textureID, BuiltinAssets::Sampler::linearClamp};
            if (frame == 6)
                render.materialOverrides.clear();

            for (size_t index = 1; index < lights.size(); ++index)
                lights[index]->enabled = frame % 2 == 0;

            lightMarkers.prepare(sceneManager.scene(), *renderContext().resources);
            const EditorFrameInput input{
                .requestPick    = (frame % 3 != 0),
                .pickX          = extent.width / 2,
                .pickY          = extent.height / 2,
                .lightDrawItems = lightMarkers.drawItems(),
            };
            const auto& renderData = renderSystem.prepare(sceneManager.scene(), sceneManagerCamera.worldPosition());
            const auto  view       = sceneManagerCamera.renderView(
                {0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)}
            );
            const auto result = renderer.render(view, renderData, input);
            CHECK(result.hasPickResult == input.requestPick, "missing picking result");
            if (input.requestPick)
                CHECK(
                    result.pickedSelectionId == expectedId,
                    "expected sphere ID {}, got {}",
                    expectedId,
                    result.pickedSelectionId
                );
        }
    };
    renderFrames();

    // Grow both frames' SSBOs; place markers outside the picking ray.
    renderer.waitIdle();
    const auto lightTemplate = std::ranges::find_if(
        scene.actors,
        [](const auto& actor) { return actor->template getComponent<LightComponent>() != nullptr; }
    );
    CHECK(lightTemplate != scene.actors.end(), "smoke test requires a light template");
    const json templateActor = Serialize(*lightTemplate);
    for (uint32_t index = 0; index < 32; ++index)
    {
        std::unique_ptr<AActor> actor;
        Deserialize(templateActor, actor);
        actor->name                 = "smoke-light-" + std::to_string(index);
        auto& light                 = *actor->getComponent<LightComponent>();
        light.castShadow            = index < 2;
        light.intensity             = 0.25f;
        actor->transform().position = center + glm::vec3{20.0f + index, 20.0f, 0.0f};
        scene.actors.push_back(std::move(actor));
    }
    sceneManager.load(scene);
    renderer.rebuildGraph();
    renderFrames();

    // Disable IBL explicitly without discarding the selected environment map.
    const auto          environmentMap  = scene.environmentMap;
    const RenderFeature originalFeature = config.rendererConfig().feature;
    RenderFeature       feature         = originalFeature;
    feature.shadow.enable               = false;
    config.setRenderFeature(feature);
    renderer.rebuildGraph();
    renderFrames();

    feature.shadow.enable             = true;
    feature.shadow.pointShadowMapSize = 512;
    config.setRenderFeature(feature);
    renderer.rebuildGraph();
    renderFrames();

    feature.ibl                = false;
    feature.ambientCoefficient = 0.25f;
    config.setRenderFeature(feature);
    for (const auto& actor : scene.actors)
        if (auto* light = actor->getComponent<LightComponent>())
            light->castShadow = false;

    sceneManager.load(scene);
    renderer.rebuildGraph();
    renderFrames();

    // Switch back to the cached IBL variant, then disable it again.
    feature.ibl = true;
    config.setRenderFeature(feature);
    sceneManager.load(scene);
    renderer.rebuildGraph();
    renderFrames();
    feature.ibl = false;
    config.setRenderFeature(feature);
    CHECK(scene.environmentMap == environmentMap);

    // Missing environment maps retain valid fallback resources without disabling IBL.
    scene.environmentMap.reset();
    feature.ibl = true;
    config.setRenderFeature(feature);
    sceneManager.load(scene);
    renderer.rebuildGraph();
    renderFrames();
    CHECK(config.rendererConfig().feature.ibl);
    feature.ibl = false;
    config.setRenderFeature(feature);

    // Zero-light scenes retain descriptors and skip SSBO reads.
    renderer.waitIdle();
    std::erase_if(
        scene.actors,
        [](const auto& actor) { return actor->template getComponent<LightComponent>() != nullptr; }
    );
    sceneManager.load(scene);
    renderer.rebuildGraph();
    renderFrames();
    renderer.waitIdle();

    // Exercise the docked UI, component inspectors, and selected-actor gizmo.
    if (renderer.vulkanContext().supportsRayTracing())
    {
        const auto originalPath = config.rendererConfig().renderPath;
        config.setRenderPath(RenderPath::RayTracing);
        renderer.rebuildGraph();
        renderFrames();
        renderer.waitIdle();
        // Both routes draw light markers through the independent editor overlay.
        auto rayLight                  = std::make_unique<ALight>();
        rayLight->transform().position = center + glm::vec3{20.0f, 20.0f, 0.0f};
        scene.actors.push_back(std::move(rayLight));
        sceneManager.load(scene);
        renderer.rebuildGraph();
        renderFrames();
        renderer.waitIdle();
        config.setRenderPath(originalPath);
        renderer.rebuildGraph();
        renderFrames();
        renderer.waitIdle();
        std::cout << "Ray tracing route and return to rasterization passed.\n";
    }

    InputManager uiInput;
    Editor       editor;
    ctx.inputManager = &uiInput;
    ctx.editor       = &editor;
    uiInput.init();
    editor.init();
    checkMovement(scene, window, sceneManager);
    editor.applyPickResult({
        .pickedSelectionId = renderActor()->selectionId(),
        .hasPickResult     = true,
    });
    auto       extent            = renderer.swapchainHandle().extent();
    const auto renderEditorFrame = [&]()
    {
        window.pollEvents();
        uiInput.tick();
        editor.applySceneActions();
        editor.tick(1.0f / 60.0f);
        const auto frame = editor.buildFrame(editor.camera(), 1.0f / 60.0f, extent.width, extent.height);
        CHECK(
            frame.view.viewport.x > 0.0f && frame.view.viewport.width < extent.width,
            "editor viewport must leave space for both docked sidebars"
        );
        const auto& renderData = renderSystem.prepare(sceneManager.scene(), sceneManagerCamera.worldPosition());
        const auto  result     = renderer.render(frame.view, renderData, frame.input);
        editor.applyPickResult(result);
        return sceneManager.findActor(editor.selectionId());
    };
    for (uint32_t frame = 0; frame < 3; ++frame)
        CHECK(renderEditorFrame() == renderActor(), "editor selection must reference the rendered actor");

    // Follow the Engine resize path: rebuild GPU targets, then refresh the UI backend.
    glfwSetWindowSize(window.nativeHandle(), 1440, 900);
    window.pollEvents();
    renderer.recreateSwapchain();
    editor.refreshUi();
    extent = renderer.swapchainHandle().extent();
    CHECK(renderEditorFrame() == renderActor());

    // Rebuild draw data for live edits; never retain references to removed actors.
    renderer.waitIdle();
    auto&      added           = sceneManager.createActor("AStaticMesh");
    const auto addedId         = added.selectionId();
    added.transform().position = center + glm::vec3{3.0f, 0.0f, 0.0f};
    added.name                 = "Renamed cube";
    editor.applyPickResult({.pickedSelectionId = addedId, .hasPickResult = true});
    CHECK(renderEditorFrame() == &added, "new actors must render and be selectable");
    renderer.waitIdle();
    sceneManager.deleteActor(addedId);
    CHECK(renderEditorFrame() == nullptr, "deleted selection must not retain an actor pointer");

    renderer.waitIdle();
    sceneManager.deleteActor(sceneManager.scene().actors.front()->selectionId());
    renderEditorFrame();
    renderer.waitIdle();
    editor.shutdown();
    uiInput.shutdown();
    ctx.editor       = nullptr;
    ctx.inputManager = nullptr;

    renderer.shutdown();
    config.setRenderPath(originalRenderer.renderPath);
    config.setRenderFeature(originalRenderer.feature);
    sceneManager.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.shutdown();
    ctx = {};
    std::cout << "GPU smoke passed: actor/component ticks, right-mouse/V navigation, free-fly and character movement, "
                 "shadow draws/clear, skybox/fallback, picking, material/texture updates, multiple/disabled/zero "
                 "lights, SSBO growth, graph rebuild, editor UI/gizmo, shutdown.\n";
}
