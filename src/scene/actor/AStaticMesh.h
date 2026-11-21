#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/AActor.h"

class RL_REGISTER_TYPE(AActor, "AStaticMesh") AStaticMesh : public AActor
{
public:
    AStaticMesh();
    void init() override;
};

#include RL_METADATA_HEADER("metadata/src/scene/actor/AStaticMesh.generated.h")
