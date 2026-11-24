#include "render/raytracing/PathSceneState.h"

#include "render/GpuScene.h"

bool PathSceneState::update(const GpuScene& scene)
{
    const auto& camera  = scene.cameraUniforms();
    const bool  changed = camera.inverseViewProjection != inverseViewProjection ||
        camera.cameraPosition != cameraPosition || scene.viewport() != viewport ||
        scene.shadingRevision() != shadingRevision || scene.sceneGeneration() != sceneGeneration ||
        scene.environmentMap() != environment;

    inverseViewProjection = camera.inverseViewProjection;
    cameraPosition        = camera.cameraPosition;
    viewport              = scene.viewport();
    shadingRevision       = scene.shadingRevision();
    sceneGeneration       = scene.sceneGeneration();
    environment           = scene.environmentMap();
    return changed;
}
