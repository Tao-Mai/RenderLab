#pragma once

#include "scene/Actor.h"
#include "scene/component/CameraComponent.h"
#include "scene/component/FreeFlyMoveComponent.h"

class FreeFlyCameraActor : public Actor
{
public:
    FreeFlyCameraActor();
    void initialize() override;
    void tick(float deltaTime) override;
    [[nodiscard]] CameraComponent& camera();
    [[nodiscard]] const CameraComponent& camera() const;
};
