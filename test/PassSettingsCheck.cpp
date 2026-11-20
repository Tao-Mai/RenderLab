#include "core/Logger.h"
#include "render/RendererConfig.h"
#include "render/device/VulkanContext.h"
#include "render/graph/PassSettings.h"
#include "render/graph/RenderPassRegistry.h"

namespace
{
struct ProbeSettings
{
    struct Controls
    {
        bool enabled = true;
        float gain = 0.5f;
    } controls;
    uint32_t size = 32;

    [[nodiscard]] bool requiresGraphRebuild(const ProbeSettings& current) const { return size != current.size; }
};

class ProbePass : public ConfigurablePass<ProbeSettings>
{
public:
    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup&)
    {
        return graph.createBuffer({.size = parameters.size});
    }
};
}

int main(int, char** argv)
{
    logger::init(argv[0]);
    VulkanContext vulkan;
    RendererConfig config;
    const PassSetup setup{vulkan, {1, 1}, {}, config};
    RenderGraph graph;
    RenderPassRegistry registry;
    registry.add<ProbePass>();
    auto description = nlohmann::json::parse(R"({
        "nodes": [
            {"type":"ProbePass","name":"first","inputs":[],"properties":{"controls":{"gain":0.1}},"ui":true},
            {"type":"ProbePass","name":"second","inputs":[],"properties":{},"ui":false}
        ],
        "outputs": {}
    })");
    PassOverrides overrides;
    const auto build = [&]
    {
        graph.releaseResources();
        (void)registry.build(graph, setup, description, nlohmann::json::object(), overrides);
    };
    build();
    const auto instances = registry.activeInstances();
    CHECK(instances.size() == 2);
    CHECK(instances[0].exposeEditor && !instances[1].exposeEditor);
    const auto& first  = *static_cast<const ProbePass*>(instances[0].object);
    const auto& second = *static_cast<const ProbePass*>(instances[1].object);
    CHECK(first.settings().controls.gain == 0.1f && second.settings().controls.gain == 0.5f);
    CHECK(first.settings().size == 32 && first.settings().controls.enabled);

    auto edited = first.settings();
    edited.controls.enabled = false;
    auto& patch = overrides["ProbePass"]["first"];
    patch = passSettingsOverrides(*instances[0].scriptedSettings, Serialize(edited));
    CHECK((patch == nlohmann::json{{"controls", {{"enabled", false}}}}));
    const bool liveUpdateRebuilds = registry.applyOverrides(overrides);
    CHECK(!liveUpdateRebuilds && !first.settings().controls.enabled && second.settings().controls.enabled);

    patch["size"] = 64;
    const bool resizeRebuilds = registry.applyOverrides(overrides);
    CHECK(resizeRebuilds);
    description["nodes"][0]["properties"]["controls"]["gain"] = 0.25;
    build();
    CHECK(registry.activeInstances()[0].object == &first);
    CHECK(first.settings().size == 64 && first.settings().controls.gain == 0.25f);
    CHECK(!first.settings().controls.enabled && second.settings().size == 32);

    overrides.clear();
    const bool resetRebuilds = registry.applyOverrides(overrides);
    CHECK(resetRebuilds && first.settings().size == 32 && first.settings().controls.enabled);
    CHECK(first.settings().controls.gain == 0.25f);
    LOG_INFO("Pass defaults, script properties, and instance overrides passed");
}
