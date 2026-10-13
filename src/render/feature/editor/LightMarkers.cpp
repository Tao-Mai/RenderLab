#include "render/feature/editor/LightMarkers.h"

#include "asset/BuiltinAssets.h"
#include "asset/SceneAsset.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/RenderResourceManager.h"
#include "scene/AActor.h"
#include "scene/component/DirectionalLightComponent.h"
#include "scene/component/PointLightComponent.h"
#include "scene/component/RectAreaLightComponent.h"
#include "scene/component/SpotLightComponent.h"
#include "scene/component/TransformComponent.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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
}

std::vector<LightMarkers::DrawItem> LightMarkers::buildDrawItems(
    const TransformComponent& transform, const LightComponent& light, uint32_t selectionId
) const
{
    std::vector<DrawItem> result;
    const glm::vec3       markerColor = light.enabled ? light.color : light.color * 0.15f;

    if (const auto* area = dynamic_cast<const RectAreaLightComponent*>(&light))
    {
        if (cube)
        {
            constexpr float    kThickness      = 0.08f;
            constexpr uint32_t kIndicesPerFace = 6;
            constexpr uint32_t kEmittingFace   = 1;
            const glm::mat4    model =
                markerTransform(transform) * glm::scale(glm::mat4{1.0f}, glm::vec3{area->areaSize, kThickness});
            const glm::vec3 housingColor = light.enabled ? glm::vec3{0.28f} : glm::vec3{0.12f};
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
    else if (dynamic_cast<const PointLightComponent*>(&light) || dynamic_cast<const SpotLightComponent*>(&light))
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
    for (DrawItem& item : result)
        item.selectionId = selectionId;
    return result;
}
