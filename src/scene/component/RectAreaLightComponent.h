#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/component/LocalLightComponent.h"

struct RL_REGISTER_TYPE(Component, "RectAreaLightComponent") RectAreaLightComponent : LocalLightComponent
{
    [[nodiscard]] glm::vec3 emissionColor() const override;

    RL_FIELD("areaSize") glm::vec2 areaSize{1.0f};
};

#include RL_METADATA_HEADER("metadata/src/scene/component/RectAreaLightComponent.generated.h")
