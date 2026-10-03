#include "scene/actor/AStaticMesh.h"

#include "scene/component/RenderComponent.h"

AStaticMesh::AStaticMesh()
{
    addDefaultComponent<RenderComponent>();
}

void AStaticMesh::init()
{
    AActor::init();
    CHECK(getComponent<RenderComponent>());
}
