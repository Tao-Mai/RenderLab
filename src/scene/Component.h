#pragma once

#include "core/MetadataAnnotations.h"

class AActor;

class RL_TYPE("Component") RL_ALLOW_UNKNOWN_FIELDS Component
{
public:
    virtual ~Component() = default;
    virtual void init(AActor* actor);
    virtual void tick(float deltaTime) = 0;

    [[nodiscard]] AActor& actor() const;

private:
    AActor* owner = nullptr;
};

#include RL_METADATA_HEADER("metadata/src/scene/Component.generated.h")
