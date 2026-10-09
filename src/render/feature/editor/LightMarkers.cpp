#include "render/feature/editor/LightMarkers.h"

#include "asset/Asset.h"
#include "asset/BuiltinAssets.h"
#include "render/feature/editor/EditorShaderData.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/RenderResourceManager.h"
#include "scene/AActor.h"
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

void LightMarkers::load(const SceneAsset& scene, RenderResourceManager& resources)
{
    reset();
    for (const auto& actor : scene.actors)
        if (actor->getComponent<LightComponent>())
            actors.push_back(actor.get());

    if (actors.empty())
        return;

    sphere = &resources.mesh(BuiltinAssets::Mesh::sphere);
    cube   = &resources.mesh(BuiltinAssets::Mesh::cube);
    arrow  = &resources.mesh(BuiltinAssets::Mesh::arrow);
}

void LightMarkers::prepare()
{
    draws.clear();
    for (const auto* actor : actors)
    {
        const auto* light     = actor->getComponent<LightComponent>();
        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(light && transform);
        const auto parts = buildDrawItems(*transform, *light, actor->selectionId());
        draws.insert(draws.end(), parts.begin(), parts.end());
    }
}

void LightMarkers::reset() noexcept
{
    actors.clear();
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

    switch (light.type)
    {
        case LightComponent::Type::Point:
        case LightComponent::Type::Spot:
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
            break;

        case LightComponent::Type::RectArea:
            if (cube)
            {
                constexpr float    kThickness      = 0.08f;
                constexpr uint32_t kIndicesPerFace = 6;
                constexpr uint32_t kEmittingFace   = 1;
                const glm::mat4    model =
                    markerTransform(transform) * glm::scale(glm::mat4{1.0f}, glm::vec3{light.areaSize, kThickness});
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
            break;

        case LightComponent::Type::Directional:
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
            break;
    }
    for (DrawItem& item : result)
        item.selectionId = selectionId;
    return result;
}

void LightMarkers::record(
    vk::raii::CommandBuffer& commandBuffer, vk::PipelineLayout pipelineLayout, std::span<const DrawItem> items
) const
{
    for (const DrawItem& part : items)
    {
        const EditorLightMarkerPushConstants pushConstants{
            .model = part.model,
            .color = glm::vec4{part.color, 1.0f},
        };
        commandBuffer.pushConstants<EditorLightMarkerPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants
        );
        part.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(part.indexCount, 1, part.firstIndex, 0, 0);
    }
}

void LightMarkers::recordPicking(
    vk::raii::CommandBuffer& commandBuffer, vk::PipelineLayout pipelineLayout, std::span<const DrawItem> items
) const
{
    for (const DrawItem& part : items)
    {
        const EditorPickingPushConstants pushConstants{
            .model       = part.model,
            .selectionId = part.selectionId,
        };
        commandBuffer.pushConstants<EditorPickingPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants
        );
        part.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(part.indexCount, 1, part.firstIndex, 0, 0);
    }
}
