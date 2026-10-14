//
// Created by 24500 on 2026/10/3.
//

#pragma once

#include "scene/system/SceneRenderData.h"

struct SceneAsset;

class RenderSystem
{
public:
    [[nodiscard]] const SceneRenderData& prepare(const SceneAsset& scene);
    void                                 reset() noexcept;

private:
    SceneRenderData data;
};
