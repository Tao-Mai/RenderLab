#pragma once

#include "scene/actor/AFreeFlyCamera.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"

// Explicit names are the persistent type IDs; serialization and UI share this list.
template<class F>
void forEachComponentType(F&& visitor)
{
    visitor.template operator()<TransformComponent>("TransformComponent");
    visitor.template operator()<CameraComponent>("CameraComponent");
    visitor.template operator()<FreeFlyMoveComponent>("FreeFlyMoveComponent");
    visitor.template operator()<CharacterMoveComponent>("CharacterMoveComponent");
    visitor.template operator()<RenderComponent>("RenderComponent");
    visitor.template operator()<LightComponent>("LightComponent");
}

template<class F>
void forEachActorType(F&& visitor)
{
    visitor.template operator()<AFreeFlyCamera>("AFreeFlyCamera");
    visitor.template operator()<ALight>("ALight");
    visitor.template operator()<AStaticMesh>("AStaticMesh");
}
