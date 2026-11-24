#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/AActor.h"

class RL_REGISTER_TYPE(AActor, "AStaticMesh") AStaticMesh : public AActor
{
public:
    AStaticMesh();
    void init() override;
};
