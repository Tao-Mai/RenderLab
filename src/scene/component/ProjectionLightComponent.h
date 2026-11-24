#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/component/LocalLightComponent.h"

struct RL_REGISTER_TYPE(Component, "ProjectionLightComponent") ProjectionLightComponent : LocalLightComponent
{
    RL_FIELD("fieldOfView") float fieldOfView = 90.0f;
};
