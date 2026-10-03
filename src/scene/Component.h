#pragma once

#include "core/Annotations.h"

class Actor;

class [[=PartialSerialize{}]] Component
{
public:
    virtual ~Component() = default;
    virtual void init(Actor* actor);
    virtual void tick(float deltaTime) = 0;

    [[nodiscard]] Actor& actor() const;

private:
    Actor* owner = nullptr;
};
