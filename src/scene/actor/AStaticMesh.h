#pragma once

#include "scene/AActor.h"

class AStaticMesh : public AActor
{
public:
    AStaticMesh();
    void init() override;
};
