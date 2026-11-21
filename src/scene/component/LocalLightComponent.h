#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/component/LightComponent.h"

// Finite lights share luminous intensity and a range.
struct RL_TYPE("LocalLightComponent") LocalLightComponent : LightComponent
{
    [[nodiscard]] glm::vec3 emissionColor() const override;

    // RectArea uses the whole emitter's normal-direction intensity, independent of area.
    RL_FIELD("luminousIntensity") float luminousIntensity = 8.0f; // cd.
    RL_FIELD("range") float range = 10.0f;
};

#include RL_METADATA_HEADER("metadata/src/scene/component/LocalLightComponent.generated.h")
