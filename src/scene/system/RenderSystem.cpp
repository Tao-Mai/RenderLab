//
// Created by 24500 on 2026/10/3.
//

#include "scene/system/RenderSystem.h"
#include "asset/SceneAsset.h"
#include "core/Context.h"
#include "core/profiling/Profiler.h"

#include "core/Logger.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"


#include <glm/geometric.hpp>


const SceneRenderData& RenderSystem::prepare(const SceneAsset& scene)
{
    CpuProfileScope timing{context().profiler, "RenderSystem"};
    data.objects.clear();
    data.lights.clear();
    data.sceneId        = scene.id;
    data.environmentMap = scene.environmentMap;
    for (const auto& actor : scene.actors)
    {
        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);
        uint32_t instanceIndex = 0;
        for (const auto* render : actor->getComponents<RenderComponent>())
        {
            data.objects.push_back({
                .mesh              = render->meshId,
                .materialOverrides = render->materialOverrides,
                .model             = transform->matrix(),
                .selectionId       = actor->selectionId(),
                .instanceIndex     = instanceIndex++,
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
        GpuLightType    lightType;
        if (dynamic_cast<const PointLightComponent*>(light))
            lightType = GpuLightType::Point;
        else if (dynamic_cast<const DirectionalLightComponent*>(light))
            lightType = GpuLightType::Directional;
        else if (dynamic_cast<const RectAreaLightComponent*>(light))
            lightType = GpuLightType::RectArea;
        else if (dynamic_cast<const SpotLightComponent*>(light))
            lightType = GpuLightType::Spot;
        else
        {
            CHECK(false, "light component has no GPU representation");
            continue;
        }

        LightData lightData{
            .color         = light->emissionColor(),
            .positionRange = {transform->position, 0.0f},
            .direction     = {direction, 0.0f},
            .lightType     = lightType,
            .shadowEnabled = light->castShadow ? 1u : 0u,
            .areaRightData = {glm::normalize(transform->rotation * glm::vec3{1.0f, 0.0f, 0.0f}), 0.0f},
        };
        if (const auto* local = dynamic_cast<const LocalLightComponent*>(light))
            lightData.positionRange.w = local->range;
        if (const auto* area = dynamic_cast<const RectAreaLightComponent*>(light))
        {
            lightData.areaSizeCone.x = area->areaSize.x;
            lightData.areaSizeCone.y = area->areaSize.y;
        }
        if (const auto* spot = dynamic_cast<const SpotLightComponent*>(light))
        {
            lightData.areaSizeCone.z = spot->cosInner;
            lightData.areaSizeCone.w = spot->cosOuter;
        }
        data.lights.push_back({.data = lightData, .enabled = light->enabled});
    }

    return data;
}

void RenderSystem::reset() noexcept { data = {}; }
