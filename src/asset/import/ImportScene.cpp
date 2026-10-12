#define GLM_ENABLE_EXPERIMENTAL
#include "asset/AssetImporter.h"

#include "asset/AssetManager.h"
#include "asset/import/GltfImport.h"
#include "asset/import/ImportId.h"
#include "core/Context.h"
#include "scene/actor/AStaticMesh.h"
#include "scene/component/RenderComponent.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/vector_relational.hpp>
#include <tiny_gltf.h>

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
        "cannot decompose glTF node {} transform",
        nodeIndex
    );
    CHECK(
        glm::all(glm::lessThanEqual(glm::abs(skew), glm::vec3{kTransformTolerance})),
        "glTF node {} has shear, which the current actor Transform cannot represent",
        nodeIndex
    );
    CHECK(
        glm::all(glm::lessThanEqual(glm::abs(perspective - glm::vec4{0, 0, 0, 1}), glm::vec4{kTransformTolerance})),
        "glTF node {} has a perspective transform",
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

void importCamera(EditorCameraConfig& camera, const tinygltf::Camera& source, const glm::mat4& world, float importScale)
{
    CHECK(source.type == "perspective", "editor camera cannot represent glTF camera type '{}'", source.type);
    const auto& projection = source.perspective;
    CHECK(std::isfinite(projection.yfov) && projection.yfov > 0 && projection.yfov < glm::pi<double>());
    CHECK(std::isfinite(projection.znear) && projection.znear > 0);

    const glm::vec3 direction = glm::normalize(glm::vec3{world * glm::vec4{0, 0, -1, 0}});
    camera.position           = glm::vec3{world[3]};
    camera.yaw                = glm::degrees(std::atan2(direction.z, direction.x));
    camera.pitch              = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));
    camera.fieldOfView        = glm::degrees(static_cast<float>(projection.yfov));
    camera.nearPlane          = static_cast<float>(projection.znear) * importScale;
    // glTF permits an infinite far plane; retain the scene-bounds fit for that case.
    if (projection.zfar > 0)
        camera.farPlane = static_cast<float>(projection.zfar) * importScale;
    CHECK(camera.farPlane > camera.nearPlane);
}
}  // namespace

AssetID<SceneAsset> AssetImporter::importScene(const ImportSceneSetting& setting)
{
    CHECK(std::isfinite(setting.importScale) && setting.importScale > 0.0f);
    GltfImport  gltf{setting.source};
    const auto& model = *gltf.model;
    SceneAsset  scene;
    scene.id = asset_import::nextId<SceneAsset>(setting.id.empty() ? gltf.source.stem().string() : setting.id.value);

    std::vector<ImportedMesh>                meshes(model.meshes.size());
    glm::vec3                                boundsMin{0.0f}, boundsMax{0.0f};
    bool                                     hasBounds = false;
    std::optional<std::pair<int, glm::mat4>> cameraNode;
    gltf.visitNodes(
        [&](uint32_t nodeIndex, const glm::mat4& world)
        {
            const auto& node = model.nodes[nodeIndex];
            if (node.camera >= 0 && !cameraNode)
            {
                CHECK(static_cast<size_t>(node.camera) < model.cameras.size());
                cameraNode = std::pair{node.camera, world};
            }
            if (node.mesh < 0)
                return;

            CHECK(static_cast<size_t>(node.mesh) < meshes.size());
            CHECK(node.skin < 0, "scene import currently supports static meshes, not glTF skin on node {}", nodeIndex);
            auto& mesh = meshes[node.mesh];
            if (!mesh.id)
            {
                auto prepared  = gltf.prepareMesh(static_cast<uint32_t>(node.mesh));
                mesh.boundsMin = prepared.boundsMin;
                mesh.boundsMax = prepared.boundsMax;
                mesh.id        = gltf.saveMesh(
                    std::move(prepared),
                    {
                               .source = gltf.source,
                               .id     = scene.id.value + "_mesh" + std::to_string(node.mesh),
                    }
                );
            }

            auto actor  = std::make_unique<AStaticMesh>();
            actor->name = node.name.empty() ? model.meshes[node.mesh].name : node.name;
            if (actor->name.empty())
                actor->name = "AStaticMesh" + std::to_string(nodeIndex);
            setTransform(actor->transform(), world, nodeIndex);
            actor->getComponent<RenderComponent>()->meshId = *mesh.id;
            scene.actors.push_back(std::move(actor));

            for (uint32_t corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local{
                    (corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                    (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                    (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z,
                };
                const glm::vec3 point{world * glm::vec4{local, 1.0f}};
                CHECK(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
                if (!hasBounds)
                {
                    boundsMin = boundsMax = point;
                    hasBounds             = true;
                }
                boundsMin = glm::min(boundsMin, point);
                boundsMax = glm::max(boundsMax, point);
            }
        },
        setting.importScale
    );

    CHECK(hasBounds, "glTF scene has no static mesh instances: {}", gltf.source.string());
    fitCamera(scene.editorCamera, boundsMin, boundsMax);
    if (cameraNode)
        importCamera(scene.editorCamera, model.cameras[cameraNode->first], cameraNode->second, setting.importScale);

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
