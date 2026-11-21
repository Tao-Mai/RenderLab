#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/component/LocalLightComponent.h"

struct RL_REGISTER_TYPE(Component, "SpotLightComponent") SpotLightComponent : LocalLightComponent
{
    void init(AActor* actor) override;

    RL_FIELD("cosInner") float cosInner = 0.9396926f;
    RL_FIELD("cosOuter") float cosOuter = 0.8660254f;
};

#include RL_METADATA_HEADER("metadata/src/scene/component/SpotLightComponent.generated.h")
