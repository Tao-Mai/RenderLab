#include "scene/component/LightComponent.h"

#include "scene/AActor.h"
#include "scene/component/LightTypes.h"

#include <algorithm>
#include <cmath>
#include <typeinfo>

LightComponent::~LightComponent() = default;

void LightComponent::init(AActor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
}

void SpotLightComponent::init(AActor* actor)
{
    LightComponent::init(actor);
    CHECK(std::isfinite(range) && range > 0.0f);
    CHECK(std::isfinite(cosOuter) && cosOuter > 0.0f && cosOuter < 1.0f);
    CHECK(std::isfinite(cosInner) && cosInner >= cosOuter && cosInner <= 1.0f);
}

glm::vec3 LightComponent::coloredEmission(float strength) const
{
    CHECK(std::isfinite(strength) && strength >= 0.0f);
    for (int axis = 0; axis < 3; ++axis)
        CHECK(std::isfinite(color[axis]) && color[axis] >= 0.0f);

    // Photometric-scaled RGB: no luminance normalization or spectral conversion.
    const glm::vec3 emission = color * strength;
    for (int axis = 0; axis < 3; ++axis)
        CHECK(std::isfinite(emission[axis]));

    return emission;
}

glm::vec3 LocalLightComponent::emissionColor() const
{
    return coloredEmission(luminousIntensity);
}

glm::vec3 DirectionalLightComponent::emissionColor() const
{
    return coloredEmission(illuminance);
}

glm::vec3 RectAreaLightComponent::emissionColor() const
{
    CHECK(std::isfinite(areaSize.x) && areaSize.x > 0.0f);
    CHECK(std::isfinite(areaSize.y) && areaSize.y > 0.0f);
    const float area = areaSize.x * areaSize.y;
    CHECK(std::isfinite(area) && area > 0.0f);
    // Whole-emitter normal-direction intensity / area gives luminance before RGB tinting.
    return coloredEmission(luminousIntensity / area);
}

LightComponent& replaceLightComponent(LightComponent& light, std::unique_ptr<LightComponent> replacement)
{
    DCHECK(replacement);
    if (typeid(light) == typeid(*replacement))
        return light;

    auto&      owner = light.actor();
    const auto found = std::ranges::find_if(owner.components, [&](const auto& item) { return item.get() == &light; });
    DCHECK(found != owner.components.end());

    replacement->color      = light.color;
    replacement->enabled    = light.enabled;
    replacement->castShadow = light.castShadow;

    const auto* sourceLocal = dynamic_cast<const LocalLightComponent*>(&light);
    const auto* sourceDirectional = dynamic_cast<const DirectionalLightComponent*>(&light);
    DCHECK(sourceLocal || sourceDirectional);
    const float strength = sourceLocal ? sourceLocal->luminousIntensity : sourceDirectional->illuminance;
    if (auto* target = dynamic_cast<LocalLightComponent*>(replacement.get()))
    {
        target->luminousIntensity = strength;
        if (sourceLocal)
            target->range = sourceLocal->range;
    }
    else
    {
        auto* directionalTarget = dynamic_cast<DirectionalLightComponent*>(replacement.get());
        DCHECK(directionalTarget);
        directionalTarget->illuminance = strength;
    }

    replacement->init(&owner);
    auto& result = *replacement;
    *found       = std::move(replacement);
    return result;
}
