#include "core/Input.h"
#include "core/Logger.h"
#include "core/Serializer.h"
#include "editor/EditorCameraConfig.h"
#include "editor/PassEditor.h"
#include "render/device/VulkanContext.h"
#include "render/graph/RenderGraphCompiler.h"
#include "render/plugins/PluginManager.h"
#include "scene/SceneManager.h"
#include "scene/actor/ALight.h"
#include "scene/component/SpotLightComponent.h"

#include <imgui.h>

namespace
{
void checkSceneMetadata()
{
    SceneManager scenes;
    scenes.init();

    SpotLightComponent spot;
    const auto fields = Serialize(spot);
    CHECK(fields.contains("color") && fields.contains("luminousIntensity") && fields.contains("cosOuter"));
    CHECK(fields.at("offlineEmission").is_null());

    SceneAsset scene;
    scene.actors.push_back(std::make_unique<ALight>(std::make_unique<SpotLightComponent>()));
    const auto saved = Serialize(scene);
    SceneAsset loaded;
    Deserialize(saved, loaded);
    CHECK(loaded.actors.size() == 1);
    CHECK(loaded.actors.front()->getComponent<SpotLightComponent>());
    CHECK(Serialize(loaded) == saved);

    const auto flags = Serialize(Modifier::Shift | Modifier::Alt);
    CHECK(flags == nlohmann::json::array({"Shift", "Alt"}));
    Modifier restored;
    Deserialize(flags, restored);
    CHECK(restored == (Modifier::Shift | Modifier::Alt));

    scenes.shutdown();
}

void checkPluginEditor(const PluginManager& plugins)
{
    const auto pass = RenderPass::create(plugins, "ToneMappingPass");
    const auto& editor = plugins.editor("ToneMappingPass");
    CHECK(editor.draw);

    const VulkanContext vulkan;
    const EditorCameraConfig camera;
    const PassEditorContext context{vulkan, camera};
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {320.0f, 240.0f};
    CHECK(io.Fonts->Build());

    ImGui::NewFrame();
    ImGui::Begin("Pass options");
    Gui::Widgets widgets;
    CHECK(!editor.draw(widgets, *pass, context));
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext();
}
}

int main(int, char** argv)
{
    logger::init(argv[0]);
    checkSceneMetadata();
    PluginManager plugins;
    plugins.loadAllPlugins();
    const auto first = RenderPass::create(plugins, "RayTracingPass");
    const auto second = RenderPass::create(plugins, "RayTracingPass");
    const auto minimal = RenderPass::create(plugins, "MinimalPathTracer");
    const auto pt = RenderPass::create(plugins, "PathTracer");
    const auto bdpt = RenderPass::create(plugins, "BDPT");
    const auto defaults = first->properties();
    CHECK(defaults == second->properties());
    CHECK(plugins.editor("RayTracingPass").draw);
    CHECK(plugins.editor("MinimalPathTracer").draw);
    CHECK(plugins.editor("PathTracer").draw);
    CHECK(plugins.editor("BDPT").draw);
    CHECK(!plugins.editor("DepthPrepass").draw);
    checkPluginEditor(plugins);

    auto scripted = defaults;
    scripted["restir"]["candidateCount"] = 16;
    CHECK(!first->setProperties(scripted));
    CHECK(second->properties() == defaults);

    auto edited = first->properties();
    edited["restir"]["temporalReuse"] = false;
    const auto overrides = passSettingsOverrides(scripted, edited);
    CHECK((overrides == nlohmann::json{{"restir", {{"temporalReuse", false}}}}));
    CHECK(!first->setProperties(edited));

    CHECK((minimal->properties() == nlohmann::json{{"maxBounces", 3}}));
    CHECK(!minimal->setProperties({{"maxBounces", 5}}));
    CHECK((pt->properties() == nlohmann::json{{"maxBounces", 2}}));
    CHECK(bdpt->properties().at("lightTracing") == true);
    auto bdptSettings = bdpt->properties();
    bdptSettings["misHeuristic"] = "Power";
    CHECK(!bdpt->setProperties(bdptSettings));
    CHECK(!first->setProperties(scripted));
    CHECK(first->properties() == scripted);

    plugins.clear();
    CHECK(first->properties() == scripted);
    CHECK(second->properties() == defaults);
    CHECK(minimal->properties().at("maxBounces") == 5);
    CHECK(bdpt->properties() == bdptSettings);
    LOG_INFO("Dynamic pass factories, independent properties, editor registration, and plugin lifetime passed");
}
