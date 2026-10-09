//
// Created by 24500 on 2026/10/3.
//

#pragma once

#include "render/RenderData.h"

#include <glm/vec3.hpp>

class RenderResourceManager;
struct SceneAsset;

class RenderSystem
{
public:
    [[nodiscard]] const RenderData&
         prepare(const SceneAsset& scene, const glm::vec3& cameraPosition, RenderResourceManager& resources);
    void reset() noexcept;

private:
    RenderData data;
};
