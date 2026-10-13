//
// Created by 24500 on 2026/10/3.
//

#include "scene/system/RenderSystem.h"
#include "asset/AssetManager.h"
#include "asset/SceneAsset.h"
#include "core/Context.h"
#include "core/profiling/Profiler.h"
#include "scene/system/Visibility.h"

#include "core/Logger.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <limits>

#include <glm/geometric.hpp>

namespace
{
struct ShadowVolume
{
    uint32_t             mask;
    std::vector<Frustum> frusta;
    uint32_t             visibleObjects = 0;
};

std::vector<ShadowVolume> shadowVolumes(const ShadowFrameData& lighting, uint32_t cascadeCount)
{
    std::vector<ShadowVolume> volumes;
    for (uint32_t index = 0; index < lighting.pointShadows.size(); ++index)
    {
        ShadowVolume volume{RenderVisibility::shadowMask(GpuLightType::Point, index), {}};
        for (const auto& vp : lighting.pointShadows[index].vp)
            volume.frusta.emplace_back(vp);
        volumes.push_back(std::move(volume));
    }
    for (uint32_t index = 0; index < lighting.spotShadows.size(); ++index)
        volumes.push_back(
            {RenderVisibility::shadowMask(GpuLightType::Spot, index), {Frustum{lighting.spotShadows[index].vp}}}
        );
    for (uint32_t index = 0; index < lighting.rectAreaShadows.size(); ++index)
        volumes.push_back(
            {RenderVisibility::shadowMask(GpuLightType::RectArea, index), {Frustum{lighting.rectAreaShadows[index].vp}}}
        );
    for (uint32_t index = 0; index < lighting.directionalShadows.size(); ++index)
        for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            volumes.push_back(
                {RenderVisibility::shadowMask(GpuLightType::Directional, index, cascade),
                 {Frustum{lighting.directionalShadows[index].cascades[cascade].vp}}}
            );
    return volumes;
}
}  // namespace

const SceneRenderData&
RenderSystem::prepare(const SceneAsset& scene, const ViewUniforms& view, const RenderFeature::Shadow& shadows)
{
    CpuProfileScope timing{context().profiler, "RenderSystem"};
    data.objects.clear();
    data.lighting       = {};
    data.sceneId        = scene.id;
    data.environmentMap = scene.environmentMap;
    for (const auto& actor : scene.actors)
    {
        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);
        const auto offset         = transform->position - glm::vec3{view.cameraPosition};
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
            .colorEmission = light->photometricEmission(),
            .positionRange = {transform->position, 0.0f},
            .direction     = {direction, 0.0f},
            .flags = {static_cast<uint32_t>(lightType), light->enabled ? 1u : 0u, light->castShadow ? 1u : 0u, 0u},
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
        data.lighting.lights.push_back(lightData);
    }

    CHECK(data.lighting.lights.size() <= std::numeric_limits<uint32_t>::max());
    {
        CpuProfileScope shadowTiming{context().profiler, "Shadow preparation"};
        data.lighting = prepareShadows(view, data.lighting.lights, shadows);
    }

    {
        CpuProfileScope cullingTiming{context().profiler, "Frustum culling"};
        const auto*     assets = context().assetManager;
        DCHECK(assets);
        const Frustum mainFrustum{view.viewProjection};
        auto          volumes     = shadowVolumes(data.lighting, shadows.cascade.count);
        uint32_t      mainVisible = 0;
        for (auto& object : data.objects)
        {
            const auto& localBounds = assets->get<MeshAsset>(object.mesh).bounds;
            const auto  worldBounds = localBounds.transformed(object.model);
            object.visibilityMask   = mainFrustum.intersects(worldBounds) ? RenderVisibility::kMainView : 0;
            if (object.visibilityMask & RenderVisibility::kMainView)
                ++mainVisible;
            for (auto& volume : volumes)
                if (std::ranges::any_of(
                        volume.frusta,
                        [&](const Frustum& frustum) { return frustum.intersects(worldBounds); }
                    ))
                {
                    object.visibilityMask |= volume.mask;
                    ++volume.visibleObjects;
                }
        }
        if (auto* profiler = context().profiler)
        {
            std::vector<ViewCullingStatistics> views{{"Main view", mainVisible}};
            for (const auto& volume : volumes)
                views.push_back({RenderVisibility::viewName(volume.mask), volume.visibleObjects});
            profiler->recordCulling(static_cast<uint32_t>(data.objects.size()), std::move(views));
        }
    }
    return data;
}

void RenderSystem::reset() noexcept { data = {}; }
