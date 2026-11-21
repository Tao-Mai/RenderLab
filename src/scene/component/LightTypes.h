#pragma once

#include "core/Metadata.h"
#include "scene/component/DirectionalLightComponent.h"
#include "scene/component/PointLightComponent.h"
#include "scene/component/RectAreaLightComponent.h"
#include "scene/component/SpotLightComponent.h"

#include <memory>

using RealtimeLightTypes =
    metadata::TypeList<PointLightComponent, DirectionalLightComponent, RectAreaLightComponent, SpotLightComponent>;

LightComponent& replaceLightComponent(LightComponent& light, std::unique_ptr<LightComponent> replacement);
