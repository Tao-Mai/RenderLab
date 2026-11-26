#include "asset/AssetImporter.h"

#include "asset/AssetManager.h"
#include "asset/import/ImportId.h"
#include "asset/import/ModelImport.h"
#include "core/Context.h"
#include "core/math/Bounds.h"
#include "scene/Scene.h"

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

void fitCamera(Camera& camera, const Aabb& bounds)
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

void importCamera(Camera& camera, const ModelImport::Camera& source)
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

AnalyticLight importLight(const ModelImport::Node& node)
{
    const auto& source = *node.light;
    AnalyticLight light;
    light.name = node.name.empty() ? "Light" + std::to_string(node.index) : node.name;
    light.color = source.color;
    light.intensity = source.intensity;
    light.openingAngle = source.openingAngle;
    light.penumbraAngle = source.penumbraAngle;
    switch (source.type)
    {
        case ModelImport::Light::Type::Point: light.type = AnalyticLightType::Point; break;
        case ModelImport::Light::Type::Directional: light.type = AnalyticLightType::Directional; break;
    }

    // Light intensity ignores node scale; its transformed -Z axis supplies the direction.
    const glm::mat4 world = node.world * source.localTransform;
    CHECK(
        glm::length(glm::vec3{world[2]}) > 0.0f &&
            glm::length(glm::cross(glm::vec3{world[0]}, glm::vec3{world[2]})) > 0.0f,
        "degenerate light transform on node {}",
        node.index
    );
    const glm::vec3 forward     = glm::normalize(-glm::vec3{world[2]});
    const glm::vec3 up          = glm::normalize(glm::cross(glm::vec3{world[0]}, forward));
    light.transform.position = glm::vec3{world[3]};
    light.transform.rotation = glm::quatLookAtRH(forward, up);
    return light;
}
}  // namespace

AssetID<Scene> AssetImporter::importScene(const ImportSceneSetting& setting)
{
    CHECK(std::isfinite(setting.importScale) && setting.importScale > 0.0f);
    auto        importer = ModelImport::open(setting.source);
    const auto& source   = importer->sourcePath();
    Scene  scene;
    scene.id = asset_import::nextId<Scene>(setting.id.empty() ? source.stem().string() : setting.id.value);
    scene.kind = setting.kind;

    std::unordered_map<uint32_t, ImportedMesh> meshes;
    std::optional<Aabb>                        sceneBounds;
    std::optional<ModelImport::Camera>         camera;

    importer->visitNodes(
        [&](const ModelImport::Node& node)
        {
            if (node.camera && !camera)
                camera = node.camera;

            if (node.light)
                scene.analyticLights.push_back(importLight(node));

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

                Object object;
                object.name = node.name.empty() ? "Object" + std::to_string(node.index) : node.name;
                if (node.meshes.size() > 1)
                    object.name += "_" + std::to_string(meshIndex);
                CHECK(
                    object.transform.setMatrix(node.world),
                    "invalid model node {} transform: {}",
                    node.index,
                    source.string()
                );
                object.meshId = *mesh.id;
                scene.objects.push_back(std::move(object));

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
    fitCamera(scene.camera, bounds);
    if (camera)
        importCamera(scene.camera, *camera);

    LOG_INFO(
        "Imported scene '{}': {} objects, {} lights, bounds [{}, {}, {}] - [{}, {}, {}]",
        scene.id,
        scene.objects.size(),
        scene.analyticLights.size(),
        bounds.min.x,
        bounds.min.y,
        bounds.min.z,
        bounds.max.x,
        bounds.max.y,
        bounds.max.z
    );
    return context().assetManager->save<Scene>(std::move(scene));
}
