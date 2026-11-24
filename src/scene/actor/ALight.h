#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/AActor.h"
#include "scene/component/LightComponent.h"

class RL_REGISTER_TYPE(AActor, "ALight") ALight : public AActor
{
public:
    ALight();
    explicit ALight(std::unique_ptr<LightComponent> light);
    void init() override;
};
