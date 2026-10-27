#include "editor/LightMarkers.h"

#include "asset/BuiltinAssets.h"
#include "asset/SceneAsset.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/RenderResourceManager.h"
#include "scene/AActor.h"
#include "scene/component/DirectionalLightComponent.h"
#include "scene/component/PointLightComponent.h"
#include "scene/component/RectAreaLightComponent.h"
#include "scene/component/SpotLightComponent.h"
#include "scene/component/TransformComponent.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace
{
[[nodiscard]] glm::mat4 markerTransform(const TransformComponent& transform)
{
    return glm::translate(glm::mat4{1.0f}, transform.position) * glm::mat4_cast(glm::normalize(transform.rotation));
}
}  // namespace

void LightMarkers::prepare(const SceneAsset& scene, RenderResourceManager& resources)
{
    draws.clear();
    for (const auto& actor : scene.actors)
    {
        const auto* light = actor->getComponent<LightComponent>();
        if (!light)
            continue;

        if (!sphere)
        {
            sphere = &resources.mesh(BuiltinAssets::Mesh::sphere);
            cube   = &resources.mesh(BuiltinAssets::Mesh::cube);
            arrow  = &resources.mesh(BuiltinAssets::Mesh::arrow);
            cone   = &resources.mesh(BuiltinAssets::Mesh::kCone);
        }

        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);
        const auto parts = buildDrawItems(*transform, *light, actor->selectionId());
        draws.insert(draws.end(), parts.begin(), parts.end());
    }
}

void LightMarkers::reset() noexcept
{
    draws.clear();
    sphere = nullptr;
    cube   = nullptr;
    arrow  = nullptr;
    cone   = nullptr;
}

std::vector<LightMarkerDrawItem> LightMarkers::buildDrawItems(
    const TransformComponent& transform, const LightComponent& light, uint32_t selectionId
) const
{
    std::vector<LightMarkerDrawItem> result;
    const glm::vec3                  markerColor = light.enabled ? light.emissionColor() : light.color * 0.15f;
    const glm::vec3                  housingColor = light.enabled ? glm::vec3{0.28f} : glm::vec3{0.12f};

    if (const auto* area = dynamic_cast<const RectAreaLightComponent*>(&light))
    {
        if (cube)
        {
            constexpr float    kThickness      = 0.08f;
            constexpr uint32_t kIndicesPerFace = 6;
            constexpr uint32_t kEmittingFace   = 1;
            const glm::mat4    model =
                markerTransform(transform) * glm::scale(glm::mat4{1.0f}, glm::vec3{area->areaSize, kThickness});
            result.reserve(6);
            for (uint32_t face = 0; face < 6; ++face)
            {
                result.push_back({
                    cube,
                    model,
                    face == kEmittingFace ? markerColor : housingColor,
                    face * kIndicesPerFace,
                    kIndicesPerFace,
                });
            }
        }
    }
    else if (dynamic_cast<const DirectionalLightComponent*>(&light))
    {
        if (arrow)
        {
            result.push_back({
                arrow,
                markerTransform(transform) * glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 1.0f, 0.75f}),
                markerColor,
                0,
                arrow->indexCount(),
            });
        }
    }
    else if (const auto* spot = dynamic_cast<const SpotLightComponent*>(&light))
    {
        DCHECK(cone);
        // Keep the slant length fixed while matching the outer half-angle.
        constexpr float kSpotMarkerSize = 0.3f;
        const float     radius          = kSpotMarkerSize * std::sqrt(1.0f - spot->cosOuter * spot->cosOuter);
        const float     height          = kSpotMarkerSize * spot->cosOuter;
        const auto model = markerTransform(transform) * glm::scale(glm::mat4{1.0f}, glm::vec3{radius, radius, height});
        constexpr size_t kEmittingSubmesh = 1;
        const auto& submeshes = cone->submeshes();
        result.reserve(submeshes.size());
        for (size_t index = 0; index < submeshes.size(); ++index)
            result.push_back({
                cone,
                model,
                index == kEmittingSubmesh ? markerColor : housingColor,
                submeshes[index].firstIndex,
                submeshes[index].indexCount,
            });
    }
    else if (dynamic_cast<const PointLightComponent*>(&light))
    {
        if (sphere)
        {
            result.push_back({
                sphere,
                glm::translate(glm::mat4{1.0f}, transform.position) * glm::scale(glm::mat4{1.0f}, glm::vec3{0.15f}),
                markerColor,
                0,
                sphere->indexCount(),
            });
        }
    }
    for (LightMarkerDrawItem& item : result)
        item.selectionId = selectionId;
    return result;
}
