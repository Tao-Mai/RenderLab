//
// Created by 24500 on 2026/10/3.
//

#include "scene/system/RenderSystem.h"
#include "asset/AssetManager.h"
#include "asset/SceneAsset.h"
#include "core/Context.h"
#include "core/profiling/Profiler.h"

#include "core/Logger.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"
#include "scene/component/MeshAreaLightComponent.h"
#include "scene/component/EnvironmentLightComponent.h"
#include "scene/component/ProjectionLightComponent.h"
#include "scene/component/GoniometricLightComponent.h"


#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <utility>

namespace
{
OfflineLightData::Model offlineLightModel(const LightComponent& light, const glm::mat4& worldFromLight)
{
    const glm::vec3 position{worldFromLight[3]};
    if (const auto* spot = dynamic_cast<const SpotLightComponent*>(&light))
        return OfflineLightData::Spot{position, glm::normalize(-glm::vec3{worldFromLight[2]}), spot->cosInner, spot->cosOuter};
    if (dynamic_cast<const PointLightComponent*>(&light))
        return OfflineLightData::Point{position};
    if (dynamic_cast<const DirectionalLightComponent*>(&light))
        return OfflineLightData::Directional{glm::normalize(-glm::vec3{worldFromLight[2]})};
    if (const auto* projection = dynamic_cast<const ProjectionLightComponent*>(&light))
        return OfflineLightData::Projection{position, glm::inverse(worldFromLight), projection->fieldOfView};
    if (dynamic_cast<const GoniometricLightComponent*>(&light))
        return OfflineLightData::Goniometric{position, glm::inverse(worldFromLight)};

    CHECK(false, "unsupported offline light component on '{}'", light.actor().name);
    std::unreachable();
}
}

const SceneRenderData& RenderSystem::prepare(const SceneAsset& scene)
{
    CpuProfileScope timing{context().profiler, "RenderSystem"};
    const auto&     assets = *context().assetManager;
    data.drawItems.clear();
    data.lights.clear();
    data.offlineLights.clear();
    data.sceneId        = scene.id;
    data.environmentMap = scene.environmentMap;
    for (const auto& actor : scene.actors)
    {
        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);
        uint32_t instanceIndex = 0;
        std::vector<uint32_t> renderDrawBegins;
        const auto renders = actor->getComponents<RenderComponent>();
        for (const auto* render : renders)
        {
            if (scene.renderMode == SceneRenderMode::Offline)
                renderDrawBegins.push_back(static_cast<uint32_t>(data.drawItems.size()));
            const auto& mesh  = assets.get<MeshAsset>(render->meshId);
            const auto  model = transform->matrix();
            for (uint32_t submeshIndex = 0; submeshIndex < mesh.submeshes.size(); ++submeshIndex)
            {
                DrawItem item{
                    .mesh          = render->meshId,
                    .submeshIndex  = submeshIndex,
                    .materialId    = mesh.submeshes[submeshIndex].materialId,
                    .offlineSurface = render->offlineSurface,
                    .model         = model,
                    .selectionId   = actor->selectionId(),
                    .instanceIndex = instanceIndex,
                };
                if (const auto override = render->materialOverrides.find(static_cast<int>(submeshIndex));
                    override != render->materialOverrides.end())
                    item.materialOverride = override->second;
                data.drawItems.push_back(std::move(item));
            }
            ++instanceIndex;
        }

        if (scene.renderMode == SceneRenderMode::Offline)
        {
            for (const auto* light : actor->getComponents<MeshAreaLightComponent>())
            {
                if (!light->enabled)
                    continue;
                DCHECK(light->renderComponentIndex < renders.size());
                DEBUG_ONLY(const auto& mesh = assets.get(renders[light->renderComponentIndex]->meshId);)
                DCHECK(light->submeshIndex < mesh.submeshes.size());
                const auto drawItemIndex = renderDrawBegins[light->renderComponentIndex] + light->submeshIndex;
                data.offlineLights.push_back({
                    .emission = light->emission,
                    .model = OfflineLightData::MeshArea{drawItemIndex, light->twoSided,
                        data.drawItems[drawItemIndex].offlineSurface->reverseOrientation},
                });
            }
            for (const auto* light : actor->getComponents<EnvironmentLightComponent>())
                if (light->enabled)
                    data.offlineLights.push_back({
                        .emission = light->emission,
                        .model = OfflineLightData::Environment{glm::inverse(transform->matrix()), light->portal},
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

        if (scene.renderMode == SceneRenderMode::Offline)
        {
            if (light->enabled)
            {
                CHECK(light->offlineEmission, "offline light emission is missing on '{}'", actor->name);
                data.offlineLights.push_back({
                    .emission = *light->offlineEmission,
                    .model = offlineLightModel(*light, transform->matrix()),
                });
            }
            continue;
        }

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
