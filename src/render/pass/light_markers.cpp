#include "render/pass/light_markers.h"

#include "asset/asset_desc.h"
#include "ecs/transform.h"
#include "render/resource/render_resource_manager.h"
#include "render/resource/shader_data.h"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{
[[nodiscard]] glm::vec3 lightDirection(const ecs::Transform& transform)
{
    return glm::normalize(transform.rotation * glm::vec3{0.0f, 0.0f, -1.0f});
}

[[nodiscard]] glm::mat4 markerTransform(const ecs::Transform& transform)
{
    const glm::vec3 forward = lightDirection(transform);
    const glm::vec3 up = std::abs(glm::dot(forward, glm::vec3{0.0f, 1.0f, 0.0f})) > 0.999f
        ? glm::vec3{0.0f, 0.0f, 1.0f}
        : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::quat rotation = glm::normalize(glm::quatLookAtRH(forward, up));
    return glm::translate(glm::mat4{1.0f}, transform.position) * glm::mat4_cast(rotation);
}
}

void LightMarkers::init(RenderResourceManager& resources)
{
    reset();
    sphere = &resources.mesh(MeshDesc::sphere);
    cube = &resources.mesh(MeshDesc::cube);
    arrow = &resources.mesh(MeshDesc::arrow);
}

void LightMarkers::reset() noexcept
{
    sphere = nullptr;
    cube = nullptr;
    arrow = nullptr;
}

std::vector<LightMarkers::Part> LightMarkers::parts(
    const ecs::Transform& transform, const ecs::Light& light) const
{
    std::vector<Part> result;
    const glm::vec3 markerColor = light.enabled ? light.color : light.color * 0.15f;

    switch (light.type)
    {
    case ecs::Light::Type::Point:
    case ecs::Light::Type::Spot:
        if (sphere)
        {
            result.push_back({
                sphere,
                glm::translate(glm::mat4{1.0f}, transform.position) *
                    glm::scale(glm::mat4{1.0f}, glm::vec3{0.15f}),
                markerColor,
                0,
                sphere->indexCount(),
            });
        }
        break;

    case ecs::Light::Type::RectArea:
        if (cube)
        {
            constexpr float thickness = 0.08f;
            constexpr uint32_t indicesPerFace = 6;
            constexpr uint32_t emittingFace = 1;
            const glm::mat4 model = markerTransform(transform) *
                glm::scale(glm::mat4{1.0f}, glm::vec3{light.areaSize, thickness});
            const glm::vec3 housingColor = light.enabled ? glm::vec3{0.28f} : glm::vec3{0.12f};
            result.reserve(6);
            for (uint32_t face = 0; face < 6; ++face)
            {
                result.push_back({
                    cube,
                    model,
                    face == emittingFace ? markerColor : housingColor,
                    face * indicesPerFace,
                    indicesPerFace,
                });
            }
        }
        break;

    case ecs::Light::Type::Directional:
        if (arrow)
        {
            constexpr float spacing = 0.32f;
            const glm::mat4 base = markerTransform(transform);
            result.reserve(9);
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    result.push_back({
                        arrow,
                        base *
                            glm::translate(
                                glm::mat4{1.0f},
                                glm::vec3{x * spacing, y * spacing, 0.0f}) *
                            glm::scale(glm::mat4{1.0f}, glm::vec3{0.75f}),
                        markerColor,
                        0,
                        arrow->indexCount(),
                    });
                }
            }
        }
        break;
    }
    return result;
}

void LightMarkers::record(
    vk::raii::CommandBuffer& commandBuffer,
    vk::PipelineLayout pipelineLayout,
    const ecs::Transform& transform,
    const ecs::Light& light) const
{
    for (const Part& part : parts(transform, light))
    {
        const LightPushConstants pushConstants{
            .model = part.model,
            .color = glm::vec4{part.color, 1.0f},
        };
        commandBuffer.pushConstants<LightPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants);
        part.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(part.indexCount, 1, part.firstIndex, 0, 0);
    }
}

void LightMarkers::recordPicking(
    vk::raii::CommandBuffer& commandBuffer,
    vk::PipelineLayout pipelineLayout,
    const ecs::Transform& transform,
    const ecs::Light& light,
    uint32_t selectionId) const
{
    for (const Part& part : parts(transform, light))
    {
        const EditorPickingPushConstants pushConstants{
            .model = part.model,
            .selectionId = selectionId,
        };
        commandBuffer.pushConstants<EditorPickingPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants);
        part.mesh->bind(commandBuffer);
        commandBuffer.drawIndexed(part.indexCount, 1, part.firstIndex, 0, 0);
    }
}
