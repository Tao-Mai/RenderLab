#pragma once

#include "render/graph/PassOverrides.h"
#include "render/graph/RenderPass.h"

struct GraphPassInstance
{
    std::string                 name;
    std::string                 type;
    std::shared_ptr<RenderPass> pass;
    nlohmann::json              defaults;
    nlohmann::json              scriptedProperties;
    bool                        exposeEditor = false;
};

class RenderGraphCompiler
{
public:
    [[nodiscard]] GraphResources compile(
        RenderGraph& graph, const PassSetup& setup, const nlohmann::json& description,
        const GraphResources& external, const PluginManager& plugins, const PassOverrides& overrides
    );
    [[nodiscard]] bool applyOverrides(const PassOverrides& overrides);
    [[nodiscard]] std::span<GraphPassInstance* const> activePasses() const { return active; }
    [[nodiscard]] const GraphPassInstance* findPass(std::string_view name) const;
    void clear() noexcept;

private:
    std::map<std::pair<std::string, std::string>, GraphPassInstance> instances;
    std::vector<GraphPassInstance*> active;
};

[[nodiscard]] bool graphUsesInputPrefix(const nlohmann::json& description, std::string_view prefix);
[[nodiscard]] nlohmann::json passSettingsOverrides(const nlohmann::json& baseline, const nlohmann::json& edited);
