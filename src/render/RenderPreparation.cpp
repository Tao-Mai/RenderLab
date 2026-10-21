#include "render/RenderPreparation.h"
#include "render/Visibility.h"

#include "asset/AssetManager.h"
#include "core/math/Bounds.h"
#include "core/profiling/Profiler.h"
#include "scene/system/SceneRenderData.h"

#include <algorithm>
#include <glm/geometric.hpp>
#include <limits>

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
    // Cascades depth-clamp upstream casters; the far plane still bounds relevant geometry.
    for (uint32_t index = 0; index < lighting.directionalShadows.size(); ++index)
        for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            volumes.push_back(
                {RenderVisibility::shadowMask(GpuLightType::Directional, index, cascade),
                 {Frustum{lighting.directionalShadows[index].cascades[cascade].vp, Frustum::NearPlane::Exclude}}}
            );
    return volumes;
}
}  // namespace

PreparedScene prepareRenderScene(
    const SceneRenderData&       scene,
    const SceneUniforms&         view,
    const RenderFeature::Shadow& shadows,
    AssetManager&                assets,
    Profiler*                    profiler
)
{
    PreparedScene prepared;
    CHECK(scene.lights.size() <= std::numeric_limits<uint32_t>::max());
    {
        CpuProfileScope timing{profiler, "Shadow preparation"};
        prepared.lighting = prepareShadows(view, scene.lights, shadows);
    }

    CpuProfileScope timing{profiler, "Frustum culling"};
    const Frustum   mainFrustum{view.viewProjection};
    auto            volumes = shadowVolumes(prepared.lighting, shadows.cascade.count);
    prepared.objects.reserve(scene.objects.size());
    uint32_t mainVisible = 0;
    for (const auto& object : scene.objects)
    {
        const auto&      localBounds = assets.get<MeshAsset>(object.mesh).bounds;
        const auto       worldBounds = localBounds.transformed(object.model);
        const auto       offset      = glm::vec3{object.model[3]} - glm::vec3{view.cameraPosition};
        ObjectVisibility visibility{
            .distanceSquared = glm::dot(offset, offset),
            .visibilityMask  = mainFrustum.intersects(worldBounds) ? RenderVisibility::kMainView : 0,
        };
        if (visibility.visibilityMask & RenderVisibility::kMainView)
            ++mainVisible;
        for (auto& volume : volumes)
            if (std::ranges::any_of(
                    volume.frusta,
                    [&](const Frustum& frustum) { return frustum.intersects(worldBounds); }
                ))
            {
                visibility.visibilityMask |= volume.mask;
                ++volume.visibleObjects;
            }
        prepared.objects.push_back(visibility);
    }
    if (profiler)
    {
        std::vector<ViewCullingStatistics> views{{"Main view", mainVisible}};
        for (const auto& volume : volumes)
            views.push_back({RenderVisibility::viewName(volume.mask), volume.visibleObjects});
        profiler->recordCulling(static_cast<uint32_t>(scene.objects.size()), std::move(views));
    }
    return prepared;
}
