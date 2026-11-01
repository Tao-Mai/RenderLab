#pragma once

#include "scene/actor/AFreeFlyCamera.h"
#include "scene/actor/ALight.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/CharacterMoveComponent.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/MeshAreaLightComponent.h"
#include "scene/component/EnvironmentLightComponent.h"
#include "scene/component/ProjectionLightComponent.h"
#include "scene/component/GoniometricLightComponent.h"

// Explicit names are the persistent type IDs; serialization and UI share this list.
template<class F>
void forEachComponentType(F&& visitor)
{
    visitor.template operator()<TransformComponent>("TransformComponent");
    visitor.template operator()<CameraComponent>("CameraComponent");
    visitor.template operator()<FreeFlyMoveComponent>("FreeFlyMoveComponent");
    visitor.template operator()<CharacterMoveComponent>("CharacterMoveComponent");
    visitor.template operator()<RenderComponent>("RenderComponent");
    visitor.template operator()<MeshAreaLightComponent>("MeshAreaLightComponent");
    visitor.template operator()<EnvironmentLightComponent>("EnvironmentLightComponent");
    visitor.template operator()<ProjectionLightComponent>("ProjectionLightComponent");
    visitor.template operator()<GoniometricLightComponent>("GoniometricLightComponent");
    forEachRealtimeLightComponentType(visitor);
}

template<class F>
void forEachActorType(F&& visitor)
{
    visitor.template operator()<AActor>("AActor");
    visitor.template operator()<AFreeFlyCamera>("AFreeFlyCamera");
    visitor.template operator()<ALight>("ALight");
    visitor.template operator()<AStaticMesh>("AStaticMesh");
}
