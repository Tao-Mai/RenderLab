#pragma once

#include "scene/AActor.h"
#include "scene/component/LightComponent.h"

class ALight : public AActor
{
public:
    explicit ALight(LightComponent::Type type = LightComponent::Type::Point);
    void init() override;
};
