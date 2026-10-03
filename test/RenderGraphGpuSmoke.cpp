#include "scene/SceneManager.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/CameraComponent.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/FreeFlyMoveComponent.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/Serializer.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "editor/Editor.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <glm/geometric.hpp>

namespace
{
void checkMovement(const SceneAsset& original, Window& window, SceneManager& manager)
{
    InputManager input;
    context().inputManager = &input;
    input.init();

    // Drive installed callbacks without OS input or changing the saved scene.
    const auto callback = glfwSetKeyCallback(window.nativeHandle(), nullptr);
    CHECK(callback != nullptr, "movement test requires the Window key callback");
    glfwSetKeyCallback(window.nativeHandle(), callback);
    const auto mouseCallback = glfwSetMouseButtonCallback(window.nativeHandle(), nullptr);
    CHECK(mouseCallback != nullptr, "navigation test requires the mouse callback");
    glfwSetMouseButtonCallback(window.nativeHandle(), mouseCallback);
    const auto key = [&](int code, int action)
    {
        callback(window.nativeHandle(), code, 0, action, 0);
    };

    SceneAsset scene;
    Deserialize(Serialize(original), scene);
    const size_t freeFlyPeerIndex = scene.actors.size();
    auto freeFlyPeer = std::make_unique<AStaticMesh>();
    freeFlyPeer->name = "FreeFly peer";
    freeFlyPeer->addComponent<FreeFlyMoveComponent>().speed = 4.0f;
    scene.actors.push_back(std::move(freeFlyPeer));

    const size_t characterPeerIndex = scene.actors.size();
    auto characterPeer = std::make_unique<AStaticMesh>();
    characterPeer->name = "Character peer";
    characterPeer->addComponent<CharacterMoveComponent>().speed = 1.0f;
    scene.actors.push_back(std::move(characterPeer));

    auto& cameraActor = *scene.actors[0];
    auto& cameraData = *cameraActor.getComponent<CameraComponent>();
    cameraData.yaw = 0.0f;
    cameraData.pitch = 45.0f;
    cameraActor.getComponent<FreeFlyMoveComponent>()->speed = 2.0f;
    // Camera navigation must tick before movement, even when component order is reversed.
    std::reverse(cameraActor.components.begin(), cameraActor.components.end());
    scene.environment.up = {0.0f, 0.0f, 3.0f};
    manager.load(scene);

    const auto camera = [&]() -> CameraComponent& { return manager.editorCamera().camera(); };
    const auto position = [&]() -> glm::vec3& { return manager.editorCamera().transform().position; };
    const auto peerPosition = [&](size_t index) -> glm::vec3&
    {
        return manager.scene().actors[index]->transform().position;
    };
    const auto expectPosition = [&](glm::vec3 expected)
    {
        CHECK(glm::length(position() - expected) < 1.0e-5f, "unexpected movement displacement");
    };
    const auto activateFreeMovement = [&]
    {
        key(GLFW_KEY_V, GLFW_PRESS);
        input.tick();
        camera().tick(0.0f);
        CHECK(camera().isNavigationActive(), "V must activate free movement");
        key(GLFW_KEY_V, GLFW_RELEASE);
        input.tick();
    };

    // WASD is inactive without right mouse or V; right mouse navigates temporarily.
    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position() = {};
    manager.tick(0.1f);
    CHECK(!camera().isNavigationActive(), "movement must require navigation");
    expectPosition({});
    mouseCallback(window.nativeHandle(), GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
    input.tick();
    manager.tick(0.1f);
    CHECK(camera().isNavigationActive(), "right mouse must enable camera movement");
    expectPosition(camera().forward() * 0.2f);
    mouseCallback(window.nativeHandle(), GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);
    key(GLFW_KEY_W, GLFW_RELEASE);
    input.tick();
    manager.tick(0.0f);
    CHECK(!camera().isNavigationActive(), "releasing right mouse must stop navigation");

    activateFreeMovement();
    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position() = {};
    peerPosition(freeFlyPeerIndex) = {};
    peerPosition(characterPeerIndex) = {};
    manager.tick(0.1f);
    expectPosition(camera().forward() * 0.2f);
    CHECK(glm::length(peerPosition(freeFlyPeerIndex) - camera().forward() * 0.4f) < 1.0e-5f,
        "FreeFlyMove must tick each actor at its own speed");
    CHECK(glm::length(peerPosition(characterPeerIndex) - camera().forward() * 0.1f) < 1.0e-5f,
        "CharacterMove must work on actors without Camera");

    key(GLFW_KEY_W, GLFW_RELEASE);
    key(GLFW_KEY_E, GLFW_PRESS);
    input.tick();
    position() = {};
    manager.tick(0.1f);
    expectPosition(camera().up() * 0.2f);
    key(GLFW_KEY_E, GLFW_RELEASE);

    std::erase_if(cameraActor.components, [](const auto& component)
    {
        return dynamic_cast<FreeFlyMoveComponent*>(component.get()) != nullptr;
    });
    cameraActor.addComponent<CharacterMoveComponent>().speed = 2.0f;
    cameraData.yaw = -90.0f;
    cameraData.pitch = 60.0f;
    scene.environment.up = {0.0f, 5.0f, 0.0f};
    manager.load(scene);
    activateFreeMovement();

    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position() = {};
    peerPosition(freeFlyPeerIndex) = {};
    peerPosition(characterPeerIndex) = {};
    manager.tick(0.1f);
    expectPosition({0.0f, 0.0f, -0.2f});
    CHECK(glm::length(peerPosition(characterPeerIndex) - glm::vec3{0.0f, 0.0f, -0.1f}) < 1.0e-5f,
        "CharacterMove must tick each actor at its own speed");
    CHECK(glm::length(peerPosition(freeFlyPeerIndex) - camera().forward() * 0.4f) < 1.0e-5f,
        "FreeFlyMove must not be limited to the camera actor");

    manager.scene().environment.up = {0.0f, 0.0f, 2.0f};
    position() = {};
    manager.tick(0.1f);
    expectPosition({0.0f, 0.2f, 0.0f});

    manager.scene().environment.up = camera().forward();
    position() = {};
    manager.tick(0.1f);
    CHECK(std::isfinite(glm::length(position())) &&
        std::abs(glm::dot(position(), manager.scene().environment.up)) < 1.0e-5f,
        "looking along Up must retain finite planar movement");

    key(GLFW_KEY_W, GLFW_RELEASE);
    for (const int code : {GLFW_KEY_E, GLFW_KEY_Q})
    {
        key(code, GLFW_PRESS);
        input.tick();
        position() = {};
        peerPosition(characterPeerIndex) = {};
        manager.tick(0.1f);
        expectPosition({});
        CHECK(glm::length(peerPosition(characterPeerIndex)) == 0.0f,
            "CharacterMove must ignore E/Q");
        key(code, GLFW_RELEASE);
    }

    input.shutdown();
    context().inputManager = nullptr;
    manager.load(original);
}
}

// Explicit integration test requiring Vulkan and configured assets. No scene is saved.
int main(int argc, char** argv)
{
    logger::init(argv[0]);
    ConfigManager config;
    AssetManager assets;
    AssetDataManager data;
    Window window;
    SceneAsset scene;
    SceneManager sceneManager;
    Renderer renderer;
    Context& ctx = context();
    ctx.config = &config;
    ctx.assetManager = &assets;
    ctx.assetDataManager = &data;
    ctx.window = &window;
    ctx.sceneManager = &sceneManager;
    ctx.renderer = &renderer;

    config.init();
    data.init();
    sceneManager.init();
    assets.init();
    window.init();
    glfwHideWindow(window.nativeHandle());
    Deserialize(Serialize(assets.get<SceneAsset>(config.initialScene())), scene);
    std::erase_if(scene.actors, [](const auto& actor)
    {
        const auto* render = actor->template getComponent<RenderComponent>();
        return render && render->meshId != MeshAsset::ID{"sphere"};
    });

    glm::vec3 center{};
    for (const auto& actor : scene.actors)
    {
        if (actor->getComponent<RenderComponent>()) center = actor->transform().position;
        if (auto* light = actor->getComponent<LightComponent>())
        {
            light->enabled = true;
            light->castShadow = true;
            light->range = 16.0f;
        }
        if (auto* camera = actor->getComponent<CameraComponent>())
        {
            camera->yaw = -90.0f;
            camera->pitch = 0.0f;
        }
    }
    for (const auto& actor : scene.actors)
        if (actor->getComponent<CameraComponent>())
            actor->transform().position = center + glm::vec3{0.0f, 0.0f, 6.0f};

    sceneManager.load(scene);
    checkMovement(scene, window, sceneManager);
    renderer.init();
    renderer.loadScene();

    const auto renderFrames = [&]
    {
        CHECK(renderer.renderData().objects.size() == 1, "smoke test requires one sphere");
        const uint32_t expectedId = renderer.renderData().objects.front().selectionId;
        const auto extent = renderer.swapchainHandle().extent();
        for (uint32_t frame = 0; frame < 8; ++frame)
        {
            window.pollEvents();
            auto& render = *renderer.renderData().objects.front().actor->getComponent<RenderComponent>();
            if (frame == 2) render.materialOverrides[0].roughness = 0.65f;
            if (frame == 4) render.materialOverrides[0].baseColorTexture = TextureBinding{
                config.rendererConfig().brdfLut.textureID, BuiltinAssets::Sampler::linearClamp};
            if (frame == 6) render.materialOverrides.clear();

            for (size_t index = 1; index < renderer.renderData().lightObjects.size(); ++index)
                renderer.renderData().lightObjects[index].actor->getComponent<LightComponent>()->enabled =
                    frame % 2 == 0;

            const EditorFrameInput input{
                .viewport = {0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height)},
                .aspectRatio = static_cast<float>(extent.width) / extent.height,
                .requestPick = (frame % 3 != 0),
                .pickX = extent.width / 2,
                .pickY = extent.height / 2,
            };
            const auto result = renderer.render(sceneManager.editorCamera().camera(), input);
            CHECK(result.hasPickResult == input.requestPick, "missing picking result");
            if (input.requestPick)
                CHECK(result.pickedSelectionId == expectedId,
                    "expected sphere ID {}, got {}", expectedId, result.pickedSelectionId);
        }
    };
    renderFrames();

    // Grow both frames' SSBOs; place markers outside the picking ray.
    renderer.waitIdle();
    const auto lightTemplate = std::ranges::find_if(scene.actors,
        [](const auto& actor) { return actor->template getComponent<LightComponent>() != nullptr; });
    CHECK(lightTemplate != scene.actors.end(), "smoke test requires a light template");
    const json templateActor = Serialize(*lightTemplate);
    for (uint32_t index = 0; index < 32; ++index)
    {
        std::unique_ptr<AActor> actor;
        Deserialize(templateActor, actor);
        actor->name = "smoke-light-" + std::to_string(index);
        auto& light = *actor->getComponent<LightComponent>();
        light.castShadow = false;
        light.intensity = 0.25f;
        actor->transform().position = center + glm::vec3{20.0f + index, 20.0f, 0.0f};
        scene.actors.push_back(std::move(actor));
    }
    sceneManager.load(scene);
    renderer.loadScene();
    renderFrames();

    // Rebuild graph allocations with shadow clear and environment fallback.
    scene.environment.environmentMap.reset();
    for (const auto& actor : scene.actors)
        if (auto* light = actor->getComponent<LightComponent>()) light->castShadow = false;

    sceneManager.load(scene);
    renderer.loadScene();
    renderFrames();

    // Zero-light scenes retain descriptors and skip SSBO reads.
    renderer.waitIdle();
    std::erase_if(scene.actors,
        [](const auto& actor) { return actor->template getComponent<LightComponent>() != nullptr; });
    sceneManager.load(scene);
    renderer.loadScene();
    renderFrames();
    renderer.waitIdle();

    // Exercise the docked UI, component inspectors, and selected-actor gizmo.
    InputManager uiInput;
    Editor editor;
    ctx.inputManager = &uiInput;
    ctx.editor = &editor;
    uiInput.init();
    editor.init();
    editor.applyPickResult({
        .pickedSelectionId = renderer.renderData().objects.front().selectionId,
        .hasPickResult = true,
    });
    const auto extent = renderer.swapchainHandle().extent();
    for (uint32_t frame = 0; frame < 3; ++frame)
    {
        window.pollEvents();
        uiInput.tick();
        const auto input = editor.buildFrame(
            sceneManager.editorCamera().camera(), 1.0f / 60.0f, extent.width, extent.height);
        CHECK(input.selectedActor == renderer.renderData().objects.front().actor,
            "editor selection must reference the rendered actor");
        const auto result = renderer.render(sceneManager.editorCamera().camera(), input);
        editor.applyPickResult(result);
    }
    renderer.waitIdle();
    editor.shutdown();
    uiInput.shutdown();
    ctx.editor = nullptr;
    ctx.inputManager = nullptr;

    renderer.shutdown();
    sceneManager.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.shutdown();
    ctx = {};
    std::cout << "GPU smoke passed: actor/component ticks, right-mouse/V navigation, free-fly and character movement, shadow draws/clear, skybox/fallback, picking, material/texture updates, multiple/disabled/zero lights, SSBO growth, graph rebuild, editor UI/gizmo, shutdown.\n";
}
