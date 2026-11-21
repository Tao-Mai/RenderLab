#pragma once

#include "core/Serializer.h"
#include "render/graph/RenderPass.h"

template <class T, class Pass = RenderPass>
class ConfigurablePass : public Pass
{
public:
    using Settings = T;

    ConfigurablePass() = default;
    ConfigurablePass(const ConfigurablePass&) = delete;
    ConfigurablePass& operator=(const ConfigurablePass&) = delete;

    [[nodiscard]] const Settings& settings() const { return parameters; }

    [[nodiscard]] nlohmann::json properties() const override { return Serialize(parameters); }

    bool setProperties(const nlohmann::json& values) override
    {
        Settings next{};
        Deserialize(values, next);
        return setSettings(next);
    }

    // The caller rebuilds the graph before recording when this returns true.
    bool setSettings(const Settings& next)
    {
        if constexpr (requires { next.validate(); })
            next.validate();

        bool rebuild = false;
        if constexpr (requires { next.requiresGraphRebuild(parameters); })
            rebuild = next.requiresGraphRebuild(parameters);
        parameters = next;
        return rebuild;
    }

protected:
    Settings parameters{};
};
