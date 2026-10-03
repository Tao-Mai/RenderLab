#include "scene/Actor.h"

Actor::Actor()
{
    addDefaultComponent<TransformComponent>();
}

void Actor::init()
{
    for (const auto& component : components)
        CHECK(component);

    CHECK(getComponent<TransformComponent>());

    for (const auto& component : components)
        component->init(this);
}

void Actor::tick(float deltaTime)
{
    for (const auto& component : components)
        component->tick(deltaTime);
}

TransformComponent& Actor::transform()
{
    return const_cast<TransformComponent&>(std::as_const(*this).transform());
}

const TransformComponent& Actor::transform() const
{
    auto* component = getComponent<TransformComponent>();
    CHECK(component);
    return *component;
}

uint32_t Actor::selectionId() const noexcept { return id; }
