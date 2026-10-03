#include "scene/actor/StaticMeshActor.h"

#include "scene/component/RenderComponent.h"

StaticMeshActor::StaticMeshActor()
{
    addComponent<RenderComponent>();
}

void StaticMeshActor::initialize()
{
    Actor::initialize();
    if (!getComponent<RenderComponent>())
        throw std::logic_error("static mesh actor requires RenderComponent: " + name);
}

void StaticMeshActor::tick(float deltaTime)
{
    tickComponents(deltaTime);
}
