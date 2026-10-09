//
// Created by 24500 on 2026/10/3.
//

#include "scene/system/RenderSystem.h"

#include "asset/ApplyOptionalFields.h"
#include "asset/AssetManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "render/resource/RenderResourceManager.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <limits>

#include <glm/geometric.hpp>

const RenderData&
RenderSystem::prepare(const SceneAsset& scene, const glm::vec3& cameraPosition, RenderResourceManager& resources)
{
    data.drawItems.clear();
    data.opaqueDrawItems.clear();
    data.transparentDrawItems.clear();
    data.lights.clear();

    for (const auto& actor : scene.actors)
    {
        const auto* render = actor->getComponent<RenderComponent>();
        if (!render)
            continue;

        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);

        GpuMesh&        mesh            = resources.mesh(render->meshId);
        const glm::mat4 model           = transform->matrix();
        const glm::vec3 offset          = transform->position - cameraPosition;
        const float     distanceSquared = glm::dot(offset, offset);
        const auto&     submeshes       = mesh.submeshes();
        for (size_t index = 0; index < submeshes.size(); ++index)
        {
            const Submesh&     submesh  = submeshes[index];
            const GpuMaterial* material = submesh.material;
            if (const auto overrideIt = render->materialOverrides.find(static_cast<int>(index));
                overrideIt != render->materialOverrides.end())
            {
                DCHECK(context().assetManager);
                const MeshAsset& meshAsset = context().assetManager->get<MeshAsset>(render->meshId);
                CHECK(index < meshAsset.submeshes.size());
                MaterialAsset asset = resources.materialAsset(meshAsset.submeshes[index].materialId);
                applyOptionalFields(asset, overrideIt->second);
                material = &resources.material(asset);
            }

            const DrawItem draw{
                &mesh,
                material,
                model,
                submesh.firstIndex,
                submesh.indexCount,
                actor->selectionId(),
                distanceSquared,
            };
            data.drawItems.push_back(draw);
            (material->renderMode() == RenderMode::Transparent ? data.transparentDrawItems : data.opaqueDrawItems)
                .push_back(draw);
        }
    }

    std::sort(
        data.transparentDrawItems.begin(),
        data.transparentDrawItems.end(),
        [](const DrawItem& left, const DrawItem& right) { return left.distanceSquared > right.distanceSquared; }
    );

    for (const auto& actor : scene.actors)
    {
        const auto* light = actor->getComponent<LightComponent>();
        if (!light)
            continue;

        const auto* transform = actor->getComponent<TransformComponent>();
        DCHECK(transform);

        const glm::vec3 direction = glm::normalize(transform->rotation * glm::vec3{0.0f, 0.0f, -1.0f});
        data.lights.push_back({
            .colorIntensity = {light->color, light->intensity},
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
