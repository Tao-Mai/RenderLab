//
// Created by 24500 on 2026/10/3.
//

#include "scene/system/RenderSystem.h"
#include "asset/SceneAsset.h"

#include "core/Logger.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <limits>

#include <glm/geometric.hpp>

const SceneRenderData& RenderSystem::prepare(const SceneAsset& scene, const glm::vec3& cameraPosition)
{
    data.objects.clear();
    data.lights.clear();
    data.sceneId = scene.id;
    data.environmentMap = scene.environmentMap;
    for (const auto& actor : scene.actors)
    {
        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);
        const auto offset         = transform->position - cameraPosition;
        uint32_t   componentIndex = 0;
        for (const auto* render : actor->getComponents<RenderComponent>())
        {
            data.objects.push_back({
                .mesh              = render->meshId,
                .materialOverrides = render->materialOverrides,
                .model             = transform->matrix(),
                .selectionId       = actor->selectionId(),
                .componentIndex    = componentIndex++,
                .distanceSquared   = glm::dot(offset, offset),
            });
        }
    }

    for (const auto& actor : scene.actors)
    {
        const auto* light = actor->getComponent<LightComponent>();
        if (!light)
            continue;

        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);

        const glm::vec3 direction = glm::normalize(transform->rotation * glm::vec3{0.0f, 0.0f, -1.0f});
        data.lights.push_back({
            .colorEmission  = light->photometricEmission(),
            .positionRange  = {transform->position, light->range},
            .direction      = {direction, 0.0f},
            .areaSizeCone   = {light->areaSize, light->cosInner, light->cosOuter},
            .flags = {static_cast<uint32_t>(light->type), light->enabled ? 1u : 0u, light->castShadow ? 1u : 0u, 0u},
            .areaRightData = {glm::normalize(transform->rotation * glm::vec3{1.0f, 0.0f, 0.0f}), 0.0f},
        });
    }

    CHECK(data.lights.size() <= std::numeric_limits<uint32_t>::max());
    return data;
}

void RenderSystem::reset() noexcept { data = {}; }
