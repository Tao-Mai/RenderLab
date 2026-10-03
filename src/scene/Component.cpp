#include "scene/Component.h"

#include "core/Logger.h"

void Component::init(Actor* actor)
{
    CHECK(actor);
    owner = actor;
}

Actor& Component::actor() const
{
    CHECK(owner);
    return *owner;
}
