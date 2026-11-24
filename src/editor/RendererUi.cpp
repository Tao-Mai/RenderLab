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
bool drawNumber(Gui::Widgets& widgets, const char* label, Properties::json& value)
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
    if (name == "postProcess")
        return "Post process";
    if (name == "shadows")
        return "Enable shadows";
    if (name == "svgf")
        return "Enable SVGF";
    if (name == "bloom")
        return "Enable bloom";
    return name.c_str();
}

bool drawParameter(Gui::Widgets& widgets, const std::string& name, Properties::json& value)
{
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
        auto& current = value.get_ref<std::string&>();
        if (name == "postProcess")
        {
            constexpr std::array kModes{"RenderLab", "Falcor"};
            const auto mode = std::ranges::find(kModes, std::string_view{current});
            if (mode != kModes.end())
            {
                auto selected = static_cast<size_t>(mode - kModes.begin());
                if (!widgets.dropdown(label, kModes, selected))
                    return false;

                current = kModes[selected];
                return true;
            }
        }

        ImGui::InputText(label, &current);
        return ImGui::IsItemDeactivatedAfterEdit();
    }
    if (value.is_number_integer())
        return drawNumber<int64_t>(widgets, label, value);

    return drawNumber<double>(widgets, label, value);
}

void drawSettings(
    Gui::Widgets&           widgets,
    Renderer&                       renderer,
    const ConfigManager&            config,
    const Properties::json& defaults,
    Properties::json&       parameters
)
{
    if (defaults.empty())
        return;

    const bool     reset = widgets.resetHeader("Graph");
    nlohmann::json options;
    for (const auto& [name, defaultValue] : defaults.items())
    {
        if (name == "ambientCoefficient")
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
    Gui::Widgets widgets;
    drawSettings(widgets, *this, config, parameterDefaults, parameters);

    auto editor   = input;
    editor.submit = [&](const RenderPass& pass, std::optional<nlohmann::json> edited)
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
    renderContext.passEditor = &editor;
    mpRenderGraph->renderUI(&renderContext, widgets);
    renderContext.passEditor = nullptr;
}
