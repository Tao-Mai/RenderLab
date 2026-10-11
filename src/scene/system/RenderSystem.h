//
// Created by 24500 on 2026/10/3.
//

#pragma once

#include "scene/system/SceneRenderData.h"

#include <glm/vec3.hpp>

struct SceneAsset;

class RenderSystem
{
public:
    [[nodiscard]] const SceneRenderData& prepare(const SceneAsset& scene, const glm::vec3& cameraPosition);
    void                                 reset() noexcept;

private:
    SceneRenderData data;
};
