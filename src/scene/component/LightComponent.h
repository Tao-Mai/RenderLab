#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/Component.h"
#include "asset/pbrt/PbrtLight.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct RL_TYPE("LightComponent") LightComponent : Component
{
    ~LightComponent() override = 0;
    void init(AActor* actor) override;
    void tick(float) override {}
    [[nodiscard]] virtual glm::vec3 emissionColor() const = 0;

    RL_FIELD("color") glm::vec3 color{1.0f};
    RL_FIELD("enabled") bool enabled = true;
    RL_FIELD("castShadow") bool castShadow = false;
    RL_FIELD("pbrtEmission") std::optional<PbrtLightEmission> pbrtEmission;

protected:
    [[nodiscard]] glm::vec3 coloredEmission(float strength) const;
};
