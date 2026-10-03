#include "scene/actor/StaticMeshActor.h"

#include "scene/component/RenderComponent.h"

StaticMeshActor::StaticMeshActor()
{
    addDefaultComponent<RenderComponent>();
}

void StaticMeshActor::init()
{
    Actor::init();
    if (!getComponent<RenderComponent>())
        throw std::logic_error("static mesh actor requires RenderComponent: " + name);
}
