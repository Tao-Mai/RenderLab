#pragma once

#include "scene/AActor.h"
#include "scene/component/CameraComponent.h"
#include "scene/component/FreeFlyMoveComponent.h"

class AFreeFlyCamera : public AActor
{
public:
    AFreeFlyCamera();
    void init() override;
    void tick(float deltaTime) override;
    [[nodiscard]] CameraComponent& camera();
    [[nodiscard]] const CameraComponent& camera() const;
};
