#include "scene/AActor.h"

AActor::AActor()
{
    addDefaultComponent<TransformComponent>();
}

void AActor::init()
{
    for (const auto& component : components)
        CHECK(component);

    CHECK(getComponent<TransformComponent>());

    for (const auto& component : components)
        component->init(this);
}

void AActor::tick(float deltaTime)
{
    for (const auto& component : components)
        component->tick(deltaTime);
}

TransformComponent& AActor::transform()
{
    return const_cast<TransformComponent&>(std::as_const(*this).transform());
}

const TransformComponent& AActor::transform() const
{
    auto* component = getComponent<TransformComponent>();
    DCHECK(component);
    return *component;
}

uint32_t AActor::selectionId() const noexcept { return id; }
