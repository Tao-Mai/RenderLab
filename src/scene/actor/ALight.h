#pragma once

#include "scene/AActor.h"
#include "scene/component/LightComponent.h"

class ALight : public AActor
{
public:
    ALight();
    explicit ALight(std::unique_ptr<LightComponent> light);
    void init() override;
};
