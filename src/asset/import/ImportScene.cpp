#include "asset/AssetImporter.h"

#include "asset/AssetManager.h"
#include "asset/import/ImportId.h"
#include "asset/import/ModelImport.h"
#include "core/Context.h"
#include "core/math/Bounds.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/actor/ALight.h"
#include "scene/component/LightTypes.h"
#include "scene/component/RenderComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

namespace
{
constexpr float kCameraFitMargin    = 1.05f;
constexpr float kMinimumSceneRadius = 0.01f;

struct ImportedMesh
{
    std::optional<MeshAsset::ID> id;
    Aabb                         bounds;
};

void fitCamera(EditorCameraConfig& camera, const Aabb& bounds)
{
    const glm::vec3 center   = (bounds.min + bounds.max) * 0.5f;
    const float     radius   = std::max(glm::length(bounds.max - bounds.min) * 0.5f, kMinimumSceneRadius);
    const float     distance = radius / std::sin(glm::radians(camera.fieldOfView * 0.5f)) * kCameraFitMargin;
    camera.position          = center + glm::vec3{0, 0, distance};
    camera.yaw               = -90.0f;
    camera.pitch             = 0.0f;
    camera.nearPlane         = radius * 0.001f;
    camera.farPlane          = distance + radius * 2.0f;
    camera.speed             = radius * 0.2f;
}

void importCamera(EditorCameraConfig& camera, const ModelImport::Camera& source)
{
    CHECK(std::isfinite(source.fieldOfView) && source.fieldOfView > 0 && source.fieldOfView < 180);
    CHECK(std::isfinite(source.nearPlane) && source.nearPlane > 0);
    const auto direction = glm::normalize(source.direction);
    camera.position      = source.position;
    camera.yaw           = glm::degrees(std::atan2(direction.z, direction.x));
    camera.pitch         = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
    camera.fieldOfView   = source.fieldOfView;
    camera.nearPlane     = source.nearPlane;
    // Infinite source projections retain the finite scene-bounds fit.
    if (source.farPlane > 0)
        camera.farPlane = source.farPlane;
    CHECK(camera.farPlane > camera.nearPlane);
}

std::unique_ptr<ALight> importLight(const ModelImport::Node& node)
{
    const auto&                     source = *node.light;
    std::unique_ptr<LightComponent> component;
    if (source.type == ModelImport::Light::Type::Directional)
    {
        auto light         = std::make_unique<DirectionalLightComponent>();
        light->illuminance = source.intensity;
        component          = std::move(light);
    }
    else
    {
        std::unique_ptr<LocalLightComponent> light;
        if (source.type == ModelImport::Light::Type::Spot)
        {
            auto spot      = std::make_unique<SpotLightComponent>();
            spot->cosInner = source.cosInner;
            spot->cosOuter = source.cosOuter;
            light          = std::move(spot);
        }
        else
        {
            light = std::make_unique<PointLightComponent>();
        }
        light->luminousIntensity = source.intensity;
        light->range             = source.range;
        component                = std::move(light);
    }
    component->color = source.color;

    // Light ranges and intensity ignore node scale; its transformed -Z axis supplies the direction.
    CHECK(
        glm::length(glm::vec3{node.world[2]}) > 0.0f &&
            glm::length(glm::cross(glm::vec3{node.world[0]}, glm::vec3{node.world[2]})) > 0.0f,
        "degenerate light transform on node {}",
        node.index
    );
    const glm::vec3 forward     = glm::normalize(-glm::vec3{node.world[2]});
    const glm::vec3 up          = glm::normalize(glm::cross(glm::vec3{node.world[0]}, forward));
    auto            actor       = std::make_unique<ALight>(std::move(component));
    actor->name                 = node.name.empty() ? "ALight" + std::to_string(node.index) : node.name;
    actor->transform().position = glm::vec3{node.world[3]};
    actor->transform().rotation = glm::quatLookAtRH(forward, up);
    return actor;
}
}  // namespace

AssetID<SceneAsset> AssetImporter::importScene(const ImportSceneSetting& setting)
{
    CHECK(std::isfinite(setting.importScale) && setting.importScale > 0.0f);
    auto        importer = ModelImport::open(setting.source);
    const auto& source   = importer->sourcePath();
    SceneAsset  scene;
    scene.id = asset_import::nextId<SceneAsset>(setting.id.empty() ? source.stem().string() : setting.id.value);

    std::unordered_map<uint32_t, ImportedMesh> meshes;
    std::optional<Aabb>                        sceneBounds;
    std::optional<ModelImport::Camera>         camera;

    importer->visitNodes(
        [&](const ModelImport::Node& node)
        {
            if (node.camera && !camera)
                camera = node.camera;

            if (node.light)
                scene.actors.push_back(importLight(node));

            for (uint32_t meshIndex : node.meshes)
            {
                auto& mesh = meshes[meshIndex];
                if (!mesh.id)
                {
                    auto prepared = importer->prepareMesh(meshIndex);
                    mesh.bounds   = {prepared.boundsMin, prepared.boundsMax};
                    mesh.id       = importer->saveMesh(
                        std::move(prepared),
                        {.source = source, .id = scene.id.value + "_mesh" + std::to_string(meshIndex)}
                    );
                }

                auto actor  = std::make_unique<AStaticMesh>();
                actor->name = node.name.empty() ? "AStaticMesh" + std::to_string(node.index) : node.name;
                if (node.meshes.size() > 1)
                    actor->name += "_" + std::to_string(meshIndex);
                CHECK(
                    actor->transform().setMatrix(node.world),
                    "invalid model node {} transform: {}",
                    node.index,
                    source.string()
                );
                actor->getComponent<RenderComponent>()->meshId = *mesh.id;
                scene.actors.push_back(std::move(actor));

                const auto worldBounds = mesh.bounds.transformed(node.world);
                for (glm::length_t axis = 0; axis < 3; ++axis)
                {
                    CHECK(
                        std::isfinite(worldBounds.min[axis]) && std::isfinite(worldBounds.max[axis]),
                        "nonfinite bounds for model node {} in '{}'",
                        node.index,
                        source.string()
                    );
                }

                if (sceneBounds)
                {
                    sceneBounds->min = glm::min(sceneBounds->min, worldBounds.min);
                    sceneBounds->max = glm::max(sceneBounds->max, worldBounds.max);
                }
                else
                    sceneBounds = worldBounds;
            }
        },
        setting.importScale
    );

    CHECK(sceneBounds, "scene has no static mesh instances: {}", source.string());
    const auto& bounds = *sceneBounds;
    fitCamera(scene.editorCamera, bounds);
    if (camera)
        importCamera(scene.editorCamera, *camera);

    LOG_INFO(
        "Imported scene '{}': {} actors, bounds [{}, {}, {}] - [{}, {}, {}]",
        scene.id,
        scene.actors.size(),
        bounds.min.x,
        bounds.min.y,
        bounds.min.z,
        bounds.max.x,
        bounds.max.y,
        bounds.max.z
    );
    return context().assetManager->save<SceneAsset>(std::move(scene));
}
