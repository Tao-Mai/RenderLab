#include "core/Logger.h"
#include "render/graph/RenderGraphCompiler.h"
#include "render/plugins/PluginManager.h"

int main(int, char** argv)
{
    logger::init(argv[0]);
    PluginManager plugins;
    plugins.loadAllPlugins();
    const auto first = RenderPass::create(plugins, "RayTracingPass");
    const auto second = RenderPass::create(plugins, "RayTracingPass");
    const auto defaults = first->properties();
    CHECK(defaults == second->properties());
    CHECK(plugins.editor("RayTracingPass").draw);
    CHECK(!plugins.editor("DepthPrepass").draw);

    auto scripted = defaults;
    scripted["restir"]["candidateCount"] = 16;
    CHECK(!first->setProperties(scripted));
    CHECK(second->properties() == defaults);

    auto edited = first->properties();
    edited["restir"]["temporalReuse"] = false;
    const auto overrides = passSettingsOverrides(scripted, edited);
    CHECK((overrides == nlohmann::json{{"restir", {{"temporalReuse", false}}}}));
    CHECK(!first->setProperties(edited));

    edited["algorithm"] = "MinimalPathTracer";
    CHECK(first->setProperties(edited));
    CHECK(first->properties().at("algorithm") == "MinimalPathTracer");
    CHECK(first->setProperties(scripted));
    CHECK(first->properties() == scripted);

    plugins.clear();
    CHECK(first->properties() == scripted);
    CHECK(second->properties() == defaults);
    LOG_INFO("Dynamic pass factories, independent properties, editor registration, and plugin lifetime passed");
}
