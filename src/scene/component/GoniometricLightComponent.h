#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/component/LocalLightComponent.h"

struct RL_REGISTER_TYPE(Component, "GoniometricLightComponent") GoniometricLightComponent : LocalLightComponent
{
};

#include RL_METADATA_HEADER("metadata/src/scene/component/GoniometricLightComponent.generated.h")
