#include "render/graph/RenderPassRegistry.h"

#include <algorithm>
#include <vector>

namespace
{
using json = nlohmann::json;

const json& field(const json& object, std::string_view name)
{
    CHECK(object.is_object(), "expected a graph object with field '{}'", name);
    const auto found = object.find(name);
    CHECK(found != object.end(), "missing graph field '{}'", name);
    return *found;
}

const std::string& nameOf(const json& value)
{
    const auto* name = value.get_ptr<const std::string*>();
    CHECK(name && !name->empty(), "graph names must be nonempty strings");
    return *name;
}

const json& resolve(const json& values, const json& reference)
{
    CHECK(reference.is_array() && reference.size() >= 2, "invalid graph reference {}", reference.dump());
    const json* value = &values;
    for (const auto& part : reference)
        value = &field(*value, nameOf(part));
    CHECK(
        !value->is_array() || (*value)[1] != RenderGraph::kInvalidHandle,
        "graph resource {} is unavailable in this configuration",
        reference.dump()
    );
    return *value;
}

void mergeSettings(json& settings, const json& overrides)
{
    for (const auto& [name, value] : overrides.items())
    {
        auto& target = settings[name];
        if (target.is_object() && value.is_object())
            mergeSettings(target, value);
        else
            target = value;
    }
}
}

nlohmann::json passSettingsOverrides(const nlohmann::json& baseline, const nlohmann::json& edited)
{
    auto overrides = json::object();
    for (const auto& [key, value] : edited.items())
    {
        const auto original = baseline.find(key);
        if (original != baseline.end() && value == *original)
            continue;
        overrides[key] = original != baseline.end() && original->is_object() && value.is_object()
            ? passSettingsOverrides(*original, value)
            : value;
    }
    return overrides;
}

bool graphUsesInput(const nlohmann::json& description, std::string_view name)
{
    const auto usesInput = [&](const json& reference)
    {
        if (!reference.is_array() || reference.size() < 2 || reference[0] != "inputs")
            return false;
        const auto* input = reference[1].get_ptr<const std::string*>();
        return input && *input == name;
    };
    for (const auto& node : field(description, "nodes"))
    {
        if (std::ranges::any_of(field(node, "inputs"), usesInput))
            return true;
    }
    return std::ranges::any_of(field(description, "outputs"), usesInput);
}

bool RenderPassRegistry::configureInstance(
    Instance& instance, const Factory& factory, std::string_view type, std::string_view name,
    const PassOverrides& overrides
)
{
    auto properties = instance.scriptedSettings;
    if (const auto byType = overrides.find(std::string{type}); byType != overrides.end())
        if (const auto found = byType->second.find(std::string{name}); found != byType->second.end())
        {
            CHECK(factory.configure && found->second.is_object(), "invalid properties for pass '{}'", name);
            mergeSettings(properties, found->second);
        }

    if (!factory.configure || properties == instance.currentSettings)
        return false;

    const bool rebuild = factory.configure(instance.pass.get(), properties);
    instance.currentSettings = std::move(properties);
    return rebuild;
}

nlohmann::json RenderPassRegistry::build(
    RenderGraph& graph, const PassSetup& setup, const nlohmann::json& description, nlohmann::json inputs,
    const PassOverrides& overrides
)
{
    const auto& nodes   = field(description, "nodes");
    const auto& outputs = field(description, "outputs");
    CHECK(nodes.is_array() && outputs.is_object(), "invalid render graph description");

    active.clear();
    json values{{"inputs", std::move(inputs)}, {"passes", json::object()}};
    auto& results = values["passes"];
    for (const auto& node : nodes)
    {
        const auto& type       = nameOf(field(node, "type"));
        const auto& name       = nameOf(field(node, "name"));
        const auto& references = field(node, "inputs");
        const auto& properties = field(node, "properties");
        const auto& ui         = field(node, "ui");
        CHECK(references.is_array(), "inputs of pass '{}' must be an array", name);
        CHECK(properties.is_object(), "properties of pass '{}' must be an object", name);
        CHECK(ui.is_boolean(), "ui flag of pass '{}' must be a boolean", name);
        CHECK(!results.contains(name), "pass instance '{}' is already in the graph", name);
        const auto factory = factories.find(type);
        CHECK(factory != factories.end(), "unknown render pass type '{}'", type);

        std::vector<const json*> arguments;
        arguments.reserve(references.size());
        for (const auto& reference : references)
            arguments.push_back(&resolve(values, reference));

        auto [entry, added] = instances.try_emplace(std::pair{type, name});
        auto& instance = entry->second;
        const auto& definition = factory->second;
        if (added)
            instance.pass = definition.create();

        if (definition.configure)
        {
            auto scriptedSettings = definition.defaults;
            mergeSettings(scriptedSettings, properties);
            instance.scriptedSettings = definition.normalizeSettings(scriptedSettings);
        }
        else
            CHECK(properties.empty(), "pass '{}' has no script properties", type);
        configureInstance(instance, definition, type, name, overrides);

        auto* pass = instance.pass.get();
        auto output = definition.setup(pass, graph, setup, arguments);
        results[name] = std::move(output);
        active.push_back({
            entry->first.second, entry->first.first, definition.type, pass,
            definition.configure ? &instance.scriptedSettings : nullptr,
            ui.get<bool>(),
        });
    }

    auto result = json::object();
    for (const auto& [name, reference] : outputs.items())
        result[name] = resolve(values, reference);
    return result;
}

bool RenderPassRegistry::applyOverrides(const PassOverrides& overrides)
{
    bool rebuild = false;
    for (const auto& pass : active)
    {
        const auto entry = instances.find({std::string{pass.type}, std::string{pass.name}});
        const auto definition = factories.find(pass.type);
        rebuild |= configureInstance(entry->second, definition->second, pass.type, pass.name, overrides);
    }
    return rebuild;
}

void RenderPassRegistry::clearInstances() noexcept
{
    active.clear();
    instances.clear();
}
