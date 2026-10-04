#include "scene/actor/AStaticMesh.h"

#include "scene/component/RenderComponent.h"
#include "asset/BuiltinAssets.h"

AStaticMesh::AStaticMesh()
{
    addDefaultComponent<RenderComponent>().meshId = BuiltinAssets::Mesh::cube;
}

void AStaticMesh::init()
{
    AActor::init();
    CHECK(getComponent<RenderComponent>());
}
