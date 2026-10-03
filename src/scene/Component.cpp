#include "scene/Component.h"

#include "core/Logger.h"

void Component::init(AActor* actor)
{
    DCHECK(actor);
    owner = actor;
}

AActor& Component::actor() const
{
    DCHECK(owner);
    return *owner;
}
