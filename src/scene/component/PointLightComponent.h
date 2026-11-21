#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/component/LocalLightComponent.h"

struct RL_REGISTER_TYPE(Component, "PointLightComponent") PointLightComponent : LocalLightComponent
{
};

#include RL_METADATA_HEADER("metadata/src/scene/component/PointLightComponent.generated.h")
