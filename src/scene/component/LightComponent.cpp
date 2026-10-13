#include "scene/component/LightComponent.h"

#include "scene/AActor.h"
#include "scene/component/LightTypes.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <typeinfo>

namespace
{
constexpr glm::vec3 kLuminanceWeights{0.2126f, 0.7152f, 0.0722f};
}

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

glm::vec4 LightComponent::photometricEmission() const
{
    CHECK(std::isfinite(intensity) && intensity >= 0.0f);
    for (int axis = 0; axis < 3; ++axis)
        CHECK(std::isfinite(color[axis]) && color[axis] >= 0.0f);

    const float emission = intensity * emissionScale();
    CHECK(std::isfinite(emission));

    // Linear Rec.709 tint with unit luminance keeps intensity independent of color.
    const float luminance = glm::dot(color, kLuminanceWeights);
    CHECK(std::isfinite(luminance));
    if (luminance == 0.0f)
        return glm::vec4{0.0f};

    return {color / luminance, emission};
}

float RectAreaLightComponent::emissionScale() const
{
    CHECK(std::isfinite(areaSize.x) && areaSize.x > 0.0f);
    CHECK(std::isfinite(areaSize.y) && areaSize.y > 0.0f);
    const float area = areaSize.x * areaSize.y;
    CHECK(std::isfinite(area) && area > 0.0f);
    // Whole-emitter normal-direction intensity / area gives luminance.
    return 1.0f / area;
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
    replacement->intensity  = light.intensity;
    replacement->enabled    = light.enabled;
    replacement->castShadow = light.castShadow;
    if (const auto* source = dynamic_cast<const LocalLightComponent*>(&light))
        if (auto* target = dynamic_cast<LocalLightComponent*>(replacement.get()))
            target->range = source->range;

    replacement->init(&owner);
    auto& result = *replacement;
    *found       = std::move(replacement);
    return result;
}
