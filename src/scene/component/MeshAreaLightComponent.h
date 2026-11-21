#pragma once

#include "core/MetadataAnnotations.h"

#include "asset/pbrt/PbrtLight.h"
#include "scene/Component.h"

struct RL_REGISTER_TYPE(Component, "MeshAreaLightComponent") MeshAreaLightComponent : Component
{
    void init(AActor* actor) override;
    void tick(float) override {}

    RL_FIELD("renderComponentIndex") uint32_t renderComponentIndex = 0;
    RL_FIELD("submeshIndex") uint32_t submeshIndex = 0;
    RL_FIELD("emission") PbrtLightEmission emission;
    RL_FIELD("twoSided") bool twoSided = false;
    RL_FIELD("enabled") bool enabled = true;
};

#include RL_METADATA_HEADER("metadata/src/scene/component/MeshAreaLightComponent.generated.h")
