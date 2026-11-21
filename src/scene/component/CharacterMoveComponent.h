#pragma once

#include "core/MetadataAnnotations.h"

#include "scene/Component.h"

struct RL_REGISTER_TYPE(Component, "CharacterMoveComponent") CharacterMoveComponent : Component
{
    void init(AActor* actor) override;
    void tick(float deltaTime) override;

    RL_FIELD("speed") float speed = 3.0f;
    RL_FIELD("sprintMultiplier") float sprintMultiplier = 3.0f;
};

#include RL_METADATA_HEADER("metadata/src/scene/component/CharacterMoveComponent.generated.h")
