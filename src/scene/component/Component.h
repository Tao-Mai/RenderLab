#pragma once

#include "core/Annotations.h"

class Actor;

class [[=PartialSerialize{}]] Component
{
public:
    virtual ~Component() = default;
    virtual bool tick(float deltaTime) { return false; }

    [[nodiscard]] Actor& actor() const;

private:
    friend class Actor;
    Actor* owner = nullptr;
};
