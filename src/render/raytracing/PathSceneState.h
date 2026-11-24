#pragma once

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include "render/ViewportRect.h"

class GpuScene;
struct GpuEnvironmentMap;

class PathSceneState
{
public:
    [[nodiscard]] bool update(const GpuScene& scene);

private:
    glm::mat4                inverseViewProjection{1.0f};
    glm::vec4                cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
    ViewportRect             viewport;
    uint64_t                 shadingRevision = 0;
    uint64_t                 sceneGeneration = 0;
    const GpuEnvironmentMap* environment     = nullptr;
};
