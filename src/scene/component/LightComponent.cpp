#include "scene/component/LightComponent.h"

#include "scene/AActor.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <numbers>

namespace
{
constexpr glm::vec3 kLuminanceWeights{0.2126f, 0.7152f, 0.0722f};
}

void LightComponent::init(AActor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
}

glm::vec4 LightComponent::photometricEmission() const
{
    CHECK(std::isfinite(intensity) && intensity >= 0.0f);
    for (int axis = 0; axis < 3; ++axis)
        CHECK(std::isfinite(color[axis]) && color[axis] >= 0.0f);

    float emission = intensity;
    switch (type)
    {
        case Type::Point:
            emission /= 4.0f * std::numbers::pi_v<float>;
            break;
        case Type::RectArea:
        {
            CHECK(std::isfinite(areaSize.x) && areaSize.x > 0.0f);
            CHECK(std::isfinite(areaSize.y) && areaSize.y > 0.0f);
            // One-sided Lambert emitter: luminous flux = pi * area * luminance.
            const float area = areaSize.x * areaSize.y;
            CHECK(std::isfinite(area) && area > 0.0f);
            emission = emission / std::numbers::pi_v<float> / area;
            break;
        }
        case Type::Directional:
        case Type::Spot:
            break;
        default:
            CHECK(false, "invalid light type: {}", static_cast<uint32_t>(type));
    }
    CHECK(std::isfinite(emission));

    // Linear Rec.709 tint with unit luminance keeps intensity independent of color.
    const float luminance = glm::dot(color, kLuminanceWeights);
    CHECK(std::isfinite(luminance));
    if (luminance == 0.0f)
        return glm::vec4{0.0f};

    return {color / luminance, emission};
}
