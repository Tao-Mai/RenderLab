//
// Created by 24500 on 2026/10/3.
//

#include "scene/system/RenderSystem.h"
#include "asset/AssetManager.h"
#include "asset/SceneAsset.h"
#include "core/Context.h"

#include "core/Logger.h"
#include "scene/component/EnvironmentLightComponent.h"
#include "scene/component/GoniometricLightComponent.h"
#include "scene/component/LightTypes.h"
#include "scene/component/ProjectionLightComponent.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"


#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <utility>

namespace
{
SceneAnalyticLight::Model lightModel(const LightComponent& light, const TransformComponent& transform)
{
    const auto position  = transform.position;
    const auto direction = light.pbrtEmission ? glm::normalize(-glm::vec3{transform.matrix()[2]})
                                              : glm::normalize(transform.rotation * glm::vec3{0, 0, -1});
    if (const auto* spot = dynamic_cast<const SpotLightComponent*>(&light))
        return SceneAnalyticLight::Spot{
            .position        = position,
            .direction       = direction,
            .cosInner        = spot->cosInner,
            .cosOuter        = spot->cosOuter,
            .range           = light.pbrtEmission ? 0.0f : spot->range,
            .falloffExponent = light.pbrtEmission ? 4.0f : 2.0f,
            .right           = glm::normalize(transform.rotation * glm::vec3{1, 0, 0}),
        };
    if (const auto* point = dynamic_cast<const PointLightComponent*>(&light))
        return SceneAnalyticLight::Point{position, light.pbrtEmission ? 0.0f : point->range};
    if (dynamic_cast<const DirectionalLightComponent*>(&light))
        return SceneAnalyticLight::Directional{direction};
    if (const auto* rectangle = dynamic_cast<const RectAreaLightComponent*>(&light))
        return SceneAnalyticLight::Rectangle{
            .position  = position,
            .direction = direction,
            .right     = glm::normalize(transform.rotation * glm::vec3{1, 0, 0}),
            .size      = rectangle->areaSize,
            .range     = rectangle->range,
        };
    if (const auto* projection = dynamic_cast<const ProjectionLightComponent*>(&light))
        return SceneAnalyticLight::Projection{position, glm::inverse(transform.matrix()), projection->fieldOfView};
    if (dynamic_cast<const GoniometricLightComponent*>(&light))
        return SceneAnalyticLight::Goniometric{position, glm::inverse(transform.matrix())};

    CHECK(false, "unsupported light component on '{}'", light.actor().name);
    std::unreachable();
}
}  // namespace

const SceneRenderData& RenderSystem::prepare(const SceneAsset& scene, uint64_t sceneGeneration)
{
    const auto& assets = *context().assetManager;
    data.drawItems.clear();
    data.analyticLights.clear();
    data.environmentLight.reset();
    data.sceneGeneration = sceneGeneration;
    data.camera          = scene.editorCamera.sceneCamera();
    data.environmentMap  = scene.environmentMap;

    for (const auto& actor : scene.actors)
    {
        const auto* transform = actor->getComponent<TransformComponent>();

        uint32_t instanceIndex = 0;
        for (const auto& component : actor->components)
        {
            const auto* render = dynamic_cast<const RenderComponent*>(component.get());
            if (!render)
                continue;

            const auto& mesh  = assets.get<MeshAsset>(render->meshId);
            const auto  model = transform->matrix();
            for (uint32_t submeshIndex = 0; submeshIndex < mesh.submeshes.size(); ++submeshIndex)
            {
                DrawItem item{
                    .mesh          = render->meshId,
                    .submeshIndex  = submeshIndex,
                    .materialId    = mesh.submeshes[submeshIndex].materialId,
                    .pbrtSurface   = render->pbrtSurface,
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

        for (const auto& component : actor->components)
        {
            if (const auto* light = dynamic_cast<const LightComponent*>(component.get()); light && light->enabled)
            {
                PbrtLightEmission emission;
                if (light->pbrtEmission)
                    emission = *light->pbrtEmission;
                else
                    emission.spectrum = PbrtRgbSpectrum{glm::dvec3{light->emissionColor()}, "srgb"};

                data.analyticLights.push_back({
                    .emission   = std::move(emission),
                    .model      = lightModel(*light, *transform),
                    .castShadow = light->castShadow,
                });
            }
            else if (const auto* env = dynamic_cast<const EnvironmentLightComponent*>(component.get());
                     env && env->enabled)
            {
                CHECK(!data.environmentLight, "scene has more than one environment light component");
                data.environmentLight = SceneEnvironmentLight{
                    .lightFromWorld = glm::inverse(transform->matrix()),
                    .portal = env->portal,
                    .emission = env->emission,
                };
            }
        }
    }

    return data;
}

void RenderSystem::reset() noexcept { data = {}; }
