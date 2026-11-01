#include "scene/component/EnvironmentLightComponent.h"
#include "scene/component/MeshAreaLightComponent.h"

#include "asset/AssetManager.h"
#include "core/Context.h"
#include "scene/AActor.h"
#include "scene/component/RenderComponent.h"

void MeshAreaLightComponent::init(AActor* actor)
{
    Component::init(actor);
    const auto renders = actor->getComponents<RenderComponent>();
    CHECK(renderComponentIndex < renders.size(), "invalid area-light render component on '{}'", actor->name);
    CHECK(renders[renderComponentIndex]->offlineSurface, "area light requires offline surface data on '{}'", actor->name);
    const auto& mesh = context().assetManager->get(renders[renderComponentIndex]->meshId);
    CHECK(submeshIndex < mesh.submeshes.size(), "invalid area-light submesh on '{}'", actor->name);
    for (const auto* other : actor->getComponents<MeshAreaLightComponent>())
        CHECK(other == this || other->renderComponentIndex != renderComponentIndex || other->submeshIndex != submeshIndex,
              "multiple area lights bind the same submesh on '{}'", actor->name);
}

void EnvironmentLightComponent::init(AActor* actor)
{
    Component::init(actor);
    CHECK(actor->getComponent<TransformComponent>());
    CHECK(portal.empty() || portal.size() == 4, "environment portal requires four vertices on '{}'", actor->name);
}
