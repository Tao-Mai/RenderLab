#pragma once

#include "scene/Actor.h"

class ALight : public Actor
{
public:
    ALight();
    void initialize() override;
    void tick(float deltaTime) override;
};
