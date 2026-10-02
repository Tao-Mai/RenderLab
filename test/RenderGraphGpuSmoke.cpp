#include "ecs/SceneManager.h"
#include "ecs/system/CameraSystem.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetDescManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/InputManager.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "ecs/component/LightComponent.h"
#include "ecs/component/CharacterMoveComponent.h"
#include "ecs/component/FreeFlyMoveComponent.h"
#include "ecs/component/RenderComponent.h"
#include "ecs/component/TransformComponent.h"
#include "ecs/system/CharacterMoveSystem.h"
#include "ecs/system/FreeFlyMoveSystem.h"
#include "render/Renderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <glm/geometric.hpp>

namespace
{
void checkMovement(const Scene::Desc& original, Window& window,
    SceneManager& manager, ecs::CameraSystem& camera)
{
    InputManager input;
    context().inputManager = &input;
    input.init();

    // Invoke the installed Window callback to test event -> command -> movement without OS key injection.
    const auto callback = glfwSetKeyCallback(window.nativeHandle(), nullptr);
    CHECK(callback != nullptr, "movement test requires the Window key callback");
    glfwSetKeyCallback(window.nativeHandle(), callback);
    const auto key = [&](int code, int action)
    {
        callback(window.nativeHandle(), code, 0, action, 0);
    };

    auto scene = original;
    const size_t freeFlyPeerIndex = scene.objects.size();
    scene.objects.push_back({
        .name = "FreeFly peer",
        .components = {
            {"Transform", ecs::TransformComponent{}},
            {"FreeFlyMove", ecs::FreeFlyMoveComponent{.speed = 4.0f}},
        },
    });
    const size_t characterPeerIndex = scene.objects.size();
    scene.objects.push_back({
        .name = "Character peer",
        .components = {
            {"Transform", ecs::TransformComponent{}},
            {"CharacterMove", ecs::CharacterMoveComponent{.speed = 1.0f}},
        },
    });
    auto object = std::ranges::find(scene.objects,
        SceneManager::editorCameraName, &Scene::Desc::Object::name);
    auto& cameraData = object->components.at("Camera").cast<ecs::CameraComponent&>();
    cameraData.yaw = 0.0f;
    cameraData.pitch = 45.0f;
    object->components.at("FreeFlyMove").cast<ecs::FreeFlyMoveComponent&>().speed = 2.0f;
    scene.environment.up = {0.0f, 0.0f, 3.0f};
    manager.load(scene);
    camera.reset();

    key(GLFW_KEY_V, GLFW_PRESS);
    input.tick();
    (void)camera.tick(true);
    CHECK(camera.isNavigationActive(), "movement test requires active navigation");
    key(GLFW_KEY_V, GLFW_RELEASE);

    ecs::FreeFlyMoveSystem freeFly;
    ecs::CharacterMoveSystem character;
    const auto position = [&]() -> glm::vec3&
    {
        return manager.registry().get<ecs::TransformComponent>(manager.editorCamera()).position;
    };
    const auto expectPosition = [&](glm::vec3 expected)
    {
        CHECK(glm::length(position() - expected) < 1.0e-5f, "unexpected movement displacement");
    };
    const auto peerPosition = [&](size_t index) -> glm::vec3&
    {
        return manager.registry().get<ecs::TransformComponent>(manager.entities()[index]).position;
    };

    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position() = {};
    CHECK(freeFly.tick(0.1f));
    CHECK(character.tick(0.1f));
    expectPosition(camera.forward() * 0.2f);
    CHECK(glm::length(peerPosition(freeFlyPeerIndex) - camera.forward() * 0.4f) < 1.0e-5f,
        "FreeFlyMove must update all matching entities at their own speed");
    CHECK(glm::length(peerPosition(characterPeerIndex) - camera.forward() * 0.1f) < 1.0e-5f,
        "CharacterMove must work on an entity without Camera");

    key(GLFW_KEY_W, GLFW_RELEASE);
    key(GLFW_KEY_E, GLFW_PRESS);
    input.tick();
    position() = {};
    CHECK(freeFly.tick(0.1f));
    expectPosition(camera.up() * 0.2f);
    key(GLFW_KEY_E, GLFW_RELEASE);

    object->components.erase("FreeFlyMove");
    object->components.emplace("CharacterMove", ecs::CharacterMoveComponent{.speed = 2.0f});
    cameraData.yaw = -90.0f;
    cameraData.pitch = 60.0f;
    scene.environment.up = {0.0f, 5.0f, 0.0f};
    manager.load(scene);

    key(GLFW_KEY_W, GLFW_PRESS);
    input.tick();
    position() = {};
    CHECK(character.tick(0.1f));
    CHECK(freeFly.tick(0.1f));
    expectPosition({0.0f, 0.0f, -0.2f});
    CHECK(glm::length(peerPosition(characterPeerIndex) - glm::vec3{0.0f, 0.0f, -0.1f}) < 1.0e-5f,
        "CharacterMove must update all matching entities at their own speed");
    CHECK(glm::length(peerPosition(freeFlyPeerIndex) - camera.forward() * 0.4f) < 1.0e-5f,
        "FreeFlyMove must not be limited to the editor camera");

    manager.scene().environment.up = {0.0f, 0.0f, 2.0f};
    position() = {};
    CHECK(character.tick(0.1f));
    expectPosition({0.0f, 0.2f, 0.0f});

    manager.scene().environment.up = camera.forward();
    position() = {};
    CHECK(character.tick(0.1f));
    CHECK(std::isfinite(glm::length(position())) &&
        std::abs(glm::dot(position(), manager.scene().environment.up)) < 1.0e-5f,
        "looking along Up must retain finite planar movement");

    key(GLFW_KEY_W, GLFW_RELEASE);
    key(GLFW_KEY_E, GLFW_PRESS);
    input.tick();
    position() = {};
    peerPosition(characterPeerIndex) = {};
    CHECK(!character.tick(0.1f));
    expectPosition({});
    CHECK(glm::length(peerPosition(characterPeerIndex)) == 0.0f, "CharacterMove must ignore E");

    key(GLFW_KEY_E, GLFW_RELEASE);
    key(GLFW_KEY_Q, GLFW_PRESS);
    input.tick();
    CHECK(!character.tick(0.1f));
    expectPosition({});
    CHECK(glm::length(peerPosition(characterPeerIndex)) == 0.0f, "CharacterMove must ignore Q");

    input.shutdown();
    context().inputManager = nullptr;
    camera.reset();
    manager.load(original);
}
}

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
    SceneManager sceneManager;
    ecs::CameraSystem camera;
    Renderer renderer;
    Context& ctx = context();
    ctx.config = &config;
    ctx.assetDescManager = &assets;
    ctx.assetDataManager = &data;
    ctx.window = &window;
    ctx.sceneManager = &sceneManager;
    ctx.cameraSystem = &camera;
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
            render->second.try_cast<ecs::RenderComponent>()->meshId != Mesh::ID{"sphere"};
    });

    glm::vec3 center{};
    for (auto& object : scene.objects)
    {
        if (object.components.contains("Render"))
            center = object.components.at("Transform").try_cast<ecs::TransformComponent>()->position;
        if (object.components.contains("Light"))
        {
            auto& light = *object.components.at("Light").try_cast<ecs::LightComponent>();
            light.enabled = true;
            light.castShadow = true;
            light.range = 16.0f;
        }
    }
    const auto cameraObject = std::ranges::find(scene.objects,
        SceneManager::editorCameraName, &Scene::Desc::Object::name);
    CHECK(cameraObject != scene.objects.end(), "smoke test requires an editor camera");
    cameraObject->components.at("Transform").cast<ecs::TransformComponent&>().position =
        center + glm::vec3{0.0f, 0.0f, 6.0f};
    auto& cameraComponent = cameraObject->components.at("Camera").cast<ecs::CameraComponent&>();
    cameraComponent.yaw = -90.0f;
    cameraComponent.pitch = 0.0f;
    sceneManager.load(scene);
    camera.reset();
    checkMovement(scene, window, sceneManager, camera);
    renderer.init();
    renderer.loadScene();

    const auto renderFrames = [&]
    {
        CHECK(renderer.gpuScene().renderItems().size() == 1, "smoke test requires one sphere");
        const uint32_t expectedId = renderer.gpuScene().renderItems().front().selectionId;
        const auto extent = renderer.swapchainHandle().extent();
        for (uint32_t frame = 0; frame < 8; ++frame)
        {
            window.pollEvents();
            auto& render = sceneManager.registry().get<ecs::RenderComponent>(
                renderer.gpuScene().renderItems().front().entity);
            if (frame == 2) render.materialOverrides[0].roughness = 0.65f;
            if (frame == 4) render.materialOverrides[0].baseColorTexture = TextureBinding{
                config.rendererConfig().brdfLut.textureID, BuiltinAssets::Sampler::linearClamp};
            if (frame == 6) render.materialOverrides.clear();

            for (size_t index = 1; index < renderer.gpuScene().lightRenderItems().size(); ++index)
            {
                auto& light = sceneManager.registry().get<ecs::LightComponent>(
                    renderer.gpuScene().lightRenderItems()[index].entity);
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
        auto& light = *object.components.at("Light").try_cast<ecs::LightComponent>();
        light.castShadow = false;
        light.intensity = 0.25f;
        object.components.at("Transform").try_cast<ecs::TransformComponent>()->position =
            center + glm::vec3{20.0f + index, 20.0f, 0.0f};
        scene.objects.push_back(std::move(object));
    }
    sceneManager.load(scene);
    renderer.loadScene();
    renderFrames();

    // Rebuild all graph allocations and exercise shadow clear + shared fallback
    // texture imports on both frame contexts after real shadow drawing.
    scene.environment.environmentMap.reset();
    for (auto& object : scene.objects)
    {
        if (object.components.contains("Light"))
            object.components.at("Light").try_cast<ecs::LightComponent>()->castShadow = false;
    }
    sceneManager.load(scene);
    renderer.loadScene();
    renderFrames();
    // A zero-light scene retains valid descriptors and skips SSBO reads.
    renderer.waitIdle();
    std::erase_if(scene.objects, [](const auto& object) { return object.components.contains("Light"); });
    sceneManager.load(scene);
    renderer.loadScene();
    renderFrames();
    renderer.waitIdle();
    renderer.shutdown();
    assets.shutdown();
    window.shutdown();
    config.shutdown();
    ctx = {};
    std::cout << "GPU smoke passed: free-fly/local-up movement, character planar/custom-up/pole movement, ignored character E/Q, shadow draws/clear, skybox/fallback, repeated picking, material/texture updates, multiple/disabled/zero lights, per-frame SSBO growth, graph rebuild, shutdown.\n";
}
