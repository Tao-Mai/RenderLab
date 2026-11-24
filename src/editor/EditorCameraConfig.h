#pragma once

#include "core/MetadataAnnotations.h"
#include "scene/system/SceneCamera.h"

#include <glm/vec3.hpp>

struct RL_RECORD EditorCameraConfig
{
    RL_FIELD("position") glm::vec3 position{0.0f};
    RL_FIELD("yaw") float     yaw              = -90.0f;
    RL_FIELD("pitch") float     pitch            = 0.0f;
    RL_FIELD("fieldOfView") float     fieldOfView      = 45.0f;
    RL_FIELD("lensRadius") float     lensRadius       = 0.0f;
    RL_FIELD("focalDistance") float     focalDistance    = 1e6f;
    RL_FIELD("nearPlane") float     nearPlane        = 0.1f;
    RL_FIELD("farPlane") float     farPlane         = 100.0f;
    RL_FIELD("mouseSensitivity") float     mouseSensitivity = 0.12f;
    RL_FIELD("speed") float     speed            = 3.0f;
    RL_FIELD("sprintMultiplier") float     sprintMultiplier = 3.0f;

    [[nodiscard]] SceneCamera sceneCamera() const
    {
        return {
            .position    = position,
            .yaw         = yaw,
            .pitch       = pitch,
            .fieldOfView = fieldOfView,
            .nearPlane   = nearPlane,
            .farPlane    = farPlane,
        };
    }
};

#include RL_METADATA_HEADER("metadata/src/editor/EditorCameraConfig.generated.h")
