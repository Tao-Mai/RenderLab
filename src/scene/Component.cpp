#include "scene/Component.h"

#include <stdexcept>

void Component::init(Actor* actor)
{
    if (!actor) throw std::invalid_argument("Component::init requires an actor");

    owner = actor;
}

Actor& Component::actor() const
{
    if (!owner) throw std::logic_error("component is not initialized");

    return *owner;
}
