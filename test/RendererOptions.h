#pragma once

#include "core/ConfigManager.h"
#include "render/Renderer.h"

namespace renderer_test
{
inline void setOptions(Renderer& renderer, const std::string& name, const nlohmann::json& values)
{
    nlohmann::json update{{name, nullptr}};
    update.update(settings::detail::flattenDictionary(nlohmann::json{{name, values}}));
    renderer.addOptions(update);
}

inline void setParameters(Renderer& renderer, const ConfigManager& config, const nlohmann::json& parameters)
{
    setOptions(renderer, kRenderGraphOptionsPrefix + settings::encodeName(config.renderGraphName()) + ":parameters", parameters);
}

inline void setPassProperties(
    Renderer& renderer,
    const ConfigManager& config,
    std::string_view type,
    std::string_view name,
    const nlohmann::json& properties
)
{
    const auto scope = kRenderGraphOptionsPrefix + settings::encodeName(config.renderGraphName()) + ":passes:" +
        settings::encodeName(type) + ':' + settings::encodeName(name);
    setOptions(renderer, scope, properties);
}
}
