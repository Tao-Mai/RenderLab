#include "render/Renderer.h"

#include "core/ConfigManager.h"
#include "core/Context.h"
#include "editor/Gui.h"
#include "editor/PassEditor.h"
#include "render/RenderContext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <optional>
#include <utility>

namespace
{
template <class T>
bool drawNumber(Falcor::Gui::Widgets& widgets, const char* label, Falcor::Properties::json& value)
{
    auto next = value.get<T>();
    if (widgets.input(label, next) && std::isfinite(next))
        value = next;

    return ImGui::IsItemDeactivatedAfterEdit();
}

const char* parameterLabel(const std::string& name)
{
    if (name == "hdr")
        return "Enable HDR";
    if (name == "ibl")
        return "IBL";
    if (name == "shadows")
        return "Enable shadows";
    if (name == "svgf")
        return "Enable SVGF";
    if (name == "bloom")
        return "Enable bloom";
    return name.c_str();
}

bool drawParameter(Falcor::Gui::Widgets& widgets, const std::string& name, Falcor::Properties::json& value)
{
    if (name == "postProcess")
    {
        constexpr auto kModes = []
        {
            constexpr auto&                          kEntries = metadata::Enum<PostProcessMode>::kValues;
            std::array<const char*, kEntries.size()> names{};
            std::ranges::transform(kEntries, names.begin(), &metadata::EnumValue<PostProcessMode>::name);
            return names;
        }();
        const auto& current = value.get_ref<const std::string&>();
        auto selected = static_cast<size_t>(std::ranges::find(kModes, std::string_view{current}) - kModes.begin());
        if (!widgets.dropdown("Post process", kModes, selected))
            return false;

        value = kModes[selected];
        return true;
    }

    const auto* label = parameterLabel(name);
    if (value.is_boolean())
    {
        auto next = value.get<bool>();
        if (!widgets.checkbox(label, next))
            return false;

        value = next;
        return true;
    }
    if (value.is_string())
    {
        ImGui::InputText(label, &value.get_ref<std::string&>());
        return ImGui::IsItemDeactivatedAfterEdit();
    }
    if (value.is_number_integer())
        return drawNumber<int64_t>(widgets, label, value);

    return drawNumber<double>(widgets, label, value);
}

void drawSettings(
    Falcor::Gui::Widgets&           widgets,
    Renderer&                       renderer,
    const ConfigManager&            config,
    const Falcor::Properties::json& defaults,
    Falcor::Properties::json&       parameters
)
{
    const auto& feature = config.rendererConfig().feature;
    const auto  visible = [&](std::string_view name)
    {
        if (name == "ambientCoefficient")
            return false;
        if (name == "postProcess")
            return feature.hdr;
        if (name == "bloom")
            return feature.hdr && feature.postProcess == PostProcessMode::RenderLab;
        return true;
    };
    const auto entries = defaults.items();
    if (std::ranges::none_of(entries, [&](const auto& entry) { return visible(entry.key()); }))
        return;

    const bool     reset = widgets.resetHeader("Graph");
    nlohmann::json options;
    for (const auto& [name, defaultValue] : entries)
    {
        if (!visible(name))
            continue;

        auto& value = parameters.at(name);
        if (reset)
            value = defaultValue;

        widgets.pushId(name.c_str());
        const bool edited = drawParameter(widgets, name, value);
        widgets.popId();
        if (!edited && !reset)
            continue;

        options[settings::encodeName(name)] = value == defaultValue ? nlohmann::json{} : nlohmann::json(value);
    }

    if (!options.is_null())
        renderer.addOption(
            kRenderGraphOptionsPrefix + settings::encodeName(config.renderGraphName()) + ":parameters",
            std::move(options)
        );
}
}  // namespace

void Renderer::renderUI(const PassEditorContext& input)
{

    const auto&          config = *context().config;
    Falcor::Gui::Widgets widgets;
    drawSettings(widgets, *this, config, parameterDefaults, parameters);

    auto editor   = input;
    editor.submit = [&](const Falcor::RenderPass& pass, std::optional<nlohmann::json> edited)
    {
        const auto scope = kRenderGraphOptionsPrefix + settings::encodeName(config.renderGraphName()) +
            ":passes:" + settings::encodeName(pass.getType()) + ':' + settings::encodeName(pass.getName());
        if (!edited)
        {
            addOption(scope, nullptr);
            return;
        }

        const auto     scripted = settings::detail::flattenDictionary(getScriptedPassProperties(pass));
        auto           changes  = settings::detail::flattenDictionary(*edited);
        nlohmann::json options{{scope, nullptr}};
        for (auto& [name, value] : changes.items())
            if (const auto original = scripted.find(name); original == scripted.end() || *original != value)
                options[scope + ':' + name] = std::move(value);
        addOptions(options);
    };
    auto renderCommands       = renderContext;
    renderCommands.passEditor = &editor;
    mpRenderGraph->renderUI(&renderCommands, widgets);
}
