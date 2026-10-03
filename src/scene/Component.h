#pragma once

#include "core/Annotations.h"

class AActor;

class [[=PartialSerialize{}]] Component
{
public:
    virtual ~Component() = default;
    virtual void init(AActor* actor);
    virtual void tick(float deltaTime) = 0;

    [[nodiscard]] AActor& actor() const;

private:
    AActor* owner = nullptr;
};
