#include "scene/Actor.h"
#include "scene/component/FreeFlyMoveComponent.h"
#include "scene/component/CharacterMoveComponent.h"

#include <typeindex>
#include <unordered_set>

Actor::Actor()
{
    addComponent<TransformComponent>();
}

Actor& Component::actor() const
{
    if (!owner) throw std::logic_error("component is not attached to an actor");
    return *owner;
}

void Actor::initialize()
{
    std::unordered_set<std::type_index> types;
    for (const auto& component : components)
    {
        if (!component) throw std::logic_error("null component on actor: " + name);
        if (!types.emplace(typeid(*component)).second)
            throw std::logic_error("duplicate component on actor: " + name);

        component->owner = this;
    }

    if (!getComponent<TransformComponent>())
        throw std::logic_error("actor requires TransformComponent: " + name);

    if (getComponent<FreeFlyMoveComponent>() && getComponent<CharacterMoveComponent>())
        throw std::logic_error("actor cannot have both movement components: " + name);
}

bool Actor::tick(float deltaTime)
{
    bool changed = false;
    for (const auto& component : components)
        changed = component->tick(deltaTime) || changed;

    return changed;
}

TransformComponent& Actor::transform()
{
    return const_cast<TransformComponent&>(std::as_const(*this).transform());
}

const TransformComponent& Actor::transform() const
{
    auto* component = getComponent<TransformComponent>();
    if (!component) throw std::logic_error("actor requires TransformComponent: " + name);
    return *component;
}

uint32_t Actor::selectionId() const noexcept { return id; }
