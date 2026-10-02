#pragma once

#include "core/Reflect.h"
#include "ecs/component/Component.h"

namespace ecs
{
struct CameraComponent
{
    float yaw = -90.0f;
    float pitch = 0.0f;
    float fieldOfView = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    float mouseSensitivity = 0.12f;
};

static_assert(Component<CameraComponent>);

REFLECT(
    CameraComponent, Camera,
    yaw,
    pitch,
    fieldOfView,
    nearPlane,
    farPlane,
    mouseSensitivity);
}
