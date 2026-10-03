#include "scene/actor/StaticMeshActor.h"

#include "scene/component/RenderComponent.h"

StaticMeshActor::StaticMeshActor()
{
    addDefaultComponent<RenderComponent>();
}

void StaticMeshActor::init()
{
    Actor::init();
    CHECK(getComponent<RenderComponent>());
}
