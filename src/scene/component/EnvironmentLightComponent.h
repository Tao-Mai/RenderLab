#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/pbrt/PbrtLight.h"
#include "scene/Component.h"

struct RL_REGISTER_TYPE(Component, "EnvironmentLightComponent") EnvironmentLightComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}

    RL_FIELD("emission") PbrtLightEmission emission;
    // Portal vertices are in engine world coordinates, independent of map rotation.
    RL_FIELD("portal") std::vector<glm::vec3> portal;
    RL_FIELD("enabled") bool enabled = true;
};

#include RL_METADATA_HEADER("metadata/src/scene/component/EnvironmentLightComponent.generated.h")
