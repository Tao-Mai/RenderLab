#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/Renderer.h"
#include "scene/SceneManager.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/RenderComponent.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>
#include <unordered_set>

// Manual import check: source file, scene ID, scale, and optional GPU check.
int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 4 || argc == 5);
    if (std::string_view{argv[2]} == "--prepare-mesh")
    {
        const std::string_view  argument = argv[3];
        std::optional<uint32_t> meshIndex;
        if (argument != "all")
        {
            uint32_t   index  = 0;
            const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), index);
            CHECK(parsed.ec == std::errc{} && parsed.ptr == argument.data() + argument.size());
            meshIndex = index;
        }
        const auto mesh = AssetImporter::prepareMesh(argv[1], meshIndex);
        LOG_INFO(
            "Mesh {} prepared: {} vertices, {} indices",
            argument,
            mesh.geometry.vertices.size(),
            mesh.geometry.indices.size()
        );
        return 0;
    }
    const std::string_view scaleArgument = argv[3];
    float                  importScale   = 0.0f;
    const auto parsed = std::from_chars(scaleArgument.data(), scaleArgument.data() + scaleArgument.size(), importScale);
    CHECK(parsed.ec == std::errc{} && parsed.ptr == scaleArgument.data() + scaleArgument.size());

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
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);

    const auto id = AssetImporter::importScene({.source = argv[1], .id = argv[2], .importScale = importScale});
    // Verify the persisted format, not only the importer-owned instance.
    const auto saved = asset_json::load<SceneAsset>(config.paths().assets / SceneAsset::dir / (id.value + ".json"));
    scenes.load(saved);
    CHECK(!scenes.scene().actors.empty());
    const auto& camera = scenes.scene().editorCamera;
    CHECK(camera.nearPlane > 0 && camera.farPlane > camera.nearPlane);
    CHECK(std::isfinite(camera.position.x) && std::isfinite(camera.position.y) && std::isfinite(camera.position.z));
    std::unordered_set<MeshAsset::ID> meshes;
    for (const auto& actor : scenes.scene().actors)
    {
        CHECK(dynamic_cast<const AStaticMesh*>(actor.get()));
        const auto* component = actor->getComponent<RenderComponent>();
        CHECK(component && &component->actor() == actor.get());
        const auto& mesh = assets.get<MeshAsset>(component->meshId);
        CHECK(mesh.meshIndex);
        if (!meshes.insert(mesh.id).second)
            continue;
        const auto geometry = data.readGeometry(mesh);
        CHECK(!geometry.vertices.empty() && !geometry.indices.empty());
        for (const auto& submesh : mesh.submeshes)
            CHECK(assets.find<MaterialAsset>(submesh.materialId));
        LOG_INFO(
            "Scene mesh '{}': {} vertices, {} indices, {} submeshes",
            mesh.id,
            geometry.vertices.size(),
            geometry.indices.size(),
            mesh.submeshes.size()
        );
    }
    LOG_INFO("Scene '{}' verified: {} actors, {} unique meshes", id, scenes.scene().actors.size(), meshes.size());

    if (argc == 5)
    {
        CHECK(std::string_view{argv[4]} == "gpu");
        CHECK(glfwInit());
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window.init();
        renderer.init();
        EditorCamera editorCamera{scenes.scene().editorCamera};
        RenderSystem system;
        renderer.rebuildGraph(system.prepare(scenes.scene()));
        const auto   extent = renderer.swapchainHandle().extent();
        const float  side   = static_cast<float>(std::min(extent.width, extent.height));
        const auto   view =
            editorCamera.renderView({(extent.width - side) * 0.5f, (extent.height - side) * 0.5f, side, side});
        renderer.render(view, system.prepare(scenes.scene()), {});
        renderer.waitIdle();
        renderer.shutdown();
        window.shutdown();
        LOG_INFO("Imported scene GPU upload/render check passed");
    }

    assets.shutdown();
    scenes.shutdown();
    data.shutdown();
    config.shutdown();
    context() = {};
}
