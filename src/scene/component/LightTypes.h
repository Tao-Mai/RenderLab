#pragma once

#include "scene/component/DirectionalLightComponent.h"
#include "scene/component/PointLightComponent.h"
#include "scene/component/RectAreaLightComponent.h"
#include "scene/component/SpotLightComponent.h"

#include <memory>

template <class F>
void forEachLightComponentType(F&& visitor)
{
    visitor.template operator()<PointLightComponent>("PointLightComponent");
    visitor.template operator()<DirectionalLightComponent>("DirectionalLightComponent");
    visitor.template operator()<RectAreaLightComponent>("RectAreaLightComponent");
    visitor.template operator()<SpotLightComponent>("SpotLightComponent");
}

[[nodiscard]] std::unique_ptr<LightComponent> createLightComponent(LightComponent::Type type);
LightComponent& replaceLightComponent(LightComponent& light, LightComponent::Type type);
