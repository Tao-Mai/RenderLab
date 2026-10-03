//
// Created by 24500 on 2026/10/3.
//

#include "scene/system/RenderSystem.h"

#include "asset/AssetManager.h"
#include "asset/ApplyOptionalFields.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/resource/RenderResourceManager.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <limits>

#include <glm/geometric.hpp>

void RenderSystem::load(const SceneAsset& scene, RenderResourceManager& resources)
{
    reset();

    for (const auto& actor : scene.actors)
    {
        const uint32_t selectionId = actor->selectionId();
        if (const auto* render = actor->getComponent<RenderComponent>())
            data.objects.push_back({&resources.mesh(render->meshId), actor.get(), selectionId});

        if (actor->getComponent<LightComponent>())
            data.lightObjects.push_back({actor.get(), selectionId});
    }

    if (!data.lightObjects.empty())
        markers.init(resources);
}

void RenderSystem::prepare(const glm::vec3& cameraPosition, RenderResourceManager& resources)
{
    data.drawItems.clear();
    data.opaqueDrawItems.clear();
    data.transparentDrawItems.clear();
    data.lightDrawItems.clear();
    data.lights.clear();

    for (const RenderObject& object : data.objects)
    {
        const auto* render = object.actor->getComponent<RenderComponent>();
        const auto* transform = object.actor->getComponent<TransformComponent>();
        DCHECK(render && transform);

        const glm::mat4 model = transform->matrix();
        const glm::vec3 offset = transform->position - cameraPosition;
        const float distanceSquared = glm::dot(offset, offset);
        const auto& submeshes = object.mesh->submeshes();
        for (size_t index = 0; index < submeshes.size(); ++index)
        {
            const Submesh& submesh = submeshes[index];
            const GpuMaterial* material = submesh.material;
            if (const auto overrideIt = render->materialOverrides.find(static_cast<int>(index));
                overrideIt != render->materialOverrides.end())
            {
                DCHECK(context().assetManager);
                const MeshAsset& mesh = context().assetManager->get<MeshAsset>(render->meshId);
                CHECK(index < mesh.submeshes.size(), "material override index out of range");
                MaterialAsset asset = resources.materialAsset(mesh.submeshes[index].materialId);
                applyOptionalFields(asset, overrideIt->second);
                material = &resources.material(asset);
            }

            const DrawItem draw{
                object.mesh, material, model, submesh.firstIndex, submesh.indexCount,
                object.selectionId, distanceSquared,
            };
            data.drawItems.push_back(draw);
            (material->renderMode() == RenderMode::Transparent
                ? data.transparentDrawItems : data.opaqueDrawItems).push_back(draw);
        }
    }

    std::sort(data.transparentDrawItems.begin(), data.transparentDrawItems.end(),
        [](const DrawItem& left, const DrawItem& right)
        {
            return left.distanceSquared > right.distanceSquared;
        });

    CHECK(data.lightObjects.size() <= std::numeric_limits<uint32_t>::max(), "too many scene lights");
    data.lights.reserve(data.lightObjects.size());
    for (const LightObject& object : data.lightObjects)
    {
        const auto* light = object.actor->getComponent<LightComponent>();
        const auto* transform = object.actor->getComponent<TransformComponent>();
        DCHECK(light && transform);

        const glm::vec3 direction = glm::normalize(transform->rotation * glm::vec3{0.0f, 0.0f, -1.0f});
        data.lights.push_back({
            .colorIntensity = {light->color, light->intensity},
            .positionRange = {transform->position, light->range},
            .direction = {direction, 0.0f},
            .areaSizeCone = {light->areaSize, light->cosInner, light->cosOuter},
            .flags = {static_cast<uint32_t>(light->type), light->enabled ? 1u : 0u,
                light->castShadow ? 1u : 0u, 0u},
        });

        const auto draws = markers.buildDrawItems(*transform, *light, object.selectionId);
        data.lightDrawItems.insert(data.lightDrawItems.end(), draws.begin(), draws.end());
    }
}

void RenderSystem::reset() noexcept
{
    data = {};
    markers.reset();
}
