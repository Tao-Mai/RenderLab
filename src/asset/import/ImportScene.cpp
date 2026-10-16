#define GLM_ENABLE_EXPERIMENTAL
#include "asset/AssetImporter.h"

#include "asset/AssetManager.h"
#include "asset/import/ImportId.h"
#include "asset/import/ModelImport.h"
#include "core/Context.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/RenderComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/vector_relational.hpp>
#include <unordered_map>

namespace
{
constexpr float kTransformTolerance = 1e-4f;
constexpr float kCameraFitMargin    = 1.05f;
constexpr float kMinimumSceneRadius = 0.01f;

struct ImportedMesh
{
    std::optional<MeshAsset::ID> id;
    glm::vec3                    boundsMin{0.0f};
    glm::vec3                    boundsMax{0.0f};
};

void setTransform(TransformComponent& transform, const glm::mat4& world, uint32_t nodeIndex)
{
    glm::vec3 skew;
    glm::vec4 perspective;
    CHECK(
        glm::decompose(world, transform.scale, transform.rotation, transform.position, skew, perspective),
        "cannot decompose model node {} transform",
        nodeIndex
    );
    CHECK(
        glm::all(glm::lessThanEqual(glm::abs(skew), glm::vec3{kTransformTolerance})),
        "model node {} has shear, which the current actor Transform cannot represent",
        nodeIndex
    );
    CHECK(
        glm::all(glm::lessThanEqual(glm::abs(perspective - glm::vec4{0, 0, 0, 1}), glm::vec4{kTransformTolerance})),
        "model node {} has a perspective transform",
        nodeIndex
    );
    transform.rotation = glm::normalize(transform.rotation);
}

void fitCamera(EditorCameraConfig& camera, glm::vec3 boundsMin, glm::vec3 boundsMax)
{
    const glm::vec3 center   = (boundsMin + boundsMax) * 0.5f;
    const float     radius   = std::max(glm::length(boundsMax - boundsMin) * 0.5f, kMinimumSceneRadius);
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
}  // namespace

AssetID<SceneAsset> AssetImporter::importScene(const ImportSceneSetting& setting)
{
    CHECK(std::isfinite(setting.importScale) && setting.importScale > 0.0f);
    auto        importer = ModelImport::open(setting.source);
    const auto& source   = importer->sourcePath();
    SceneAsset  scene;
    scene.id = asset_import::nextId<SceneAsset>(setting.id.empty() ? source.stem().string() : setting.id.value);

    std::unordered_map<uint32_t, ImportedMesh> meshes;
    glm::vec3                                  boundsMin{0.0f}, boundsMax{0.0f};
    bool                                       hasBounds = false;
    std::optional<ModelImport::Camera>         camera;
    importer->visitNodes(
        [&](const ModelImport::Node& node)
        {
            if (node.camera && !camera)
                camera = node.camera;
            for (uint32_t meshIndex : node.meshes)
            {
                auto& mesh = meshes[meshIndex];
                if (!mesh.id)
                {
                    auto prepared  = importer->prepareMesh(meshIndex);
                    mesh.boundsMin = prepared.boundsMin;
                    mesh.boundsMax = prepared.boundsMax;
                    mesh.id        = importer->saveMesh(
                        std::move(prepared),
                        {
                                   .source = source,
                                   .id     = scene.id.value + "_mesh" + std::to_string(meshIndex),
                        }
                    );
                }

                auto actor  = std::make_unique<AStaticMesh>();
                actor->name = node.name.empty() ? "AStaticMesh" + std::to_string(node.index) : node.name;
                if (node.meshes.size() > 1)
                    actor->name += "_" + std::to_string(meshIndex);
                setTransform(actor->transform(), node.world, node.index);
                actor->getComponent<RenderComponent>()->meshId = *mesh.id;
                scene.actors.push_back(std::move(actor));

                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    const glm::vec3 local{
                        (corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                        (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                        (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z,
                    };
                    const glm::vec3 point{node.world * glm::vec4{local, 1.0f}};
                    CHECK(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
                    if (!hasBounds)
                    {
                        boundsMin = boundsMax = point;
                        hasBounds             = true;
                    }
                    boundsMin = glm::min(boundsMin, point);
                    boundsMax = glm::max(boundsMax, point);
                }
            }
        },
        setting.importScale
    );

    CHECK(hasBounds, "scene has no static mesh instances: {}", source.string());
    fitCamera(scene.editorCamera, boundsMin, boundsMax);
    if (camera)
        importCamera(scene.editorCamera, *camera);

    LOG_INFO(
        "Imported scene '{}': {} actors, bounds [{}, {}, {}] - [{}, {}, {}]",
        scene.id,
        scene.actors.size(),
        boundsMin.x,
        boundsMin.y,
        boundsMin.z,
        boundsMax.x,
        boundsMax.y,
        boundsMax.z
    );
    return context().assetManager->save<SceneAsset>(std::move(scene));
}
