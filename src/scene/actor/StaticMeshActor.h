#pragma once

#include "scene/Actor.h"

class StaticMeshActor : public Actor
{
public:
    StaticMeshActor();
    void initialize() override;
};
