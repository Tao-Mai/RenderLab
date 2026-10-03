#include "scene/Component.h"

#include "core/Logger.h"

void Component::init(Actor* actor)
{
    DCHECK(actor);
    owner = actor;
}

Actor& Component::actor() const
{
    DCHECK(owner);
    return *owner;
}
