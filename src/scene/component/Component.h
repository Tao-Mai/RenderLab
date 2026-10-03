#pragma once

#include "core/Annotations.h"

class Actor;

class [[=PartialSerialize{}]] Component
{
public:
    virtual ~Component() = default;
    virtual void tick(float deltaTime) = 0;

    [[nodiscard]] Actor& actor() const;

private:
    friend class Actor;
    Actor* owner = nullptr;
};
