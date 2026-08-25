#pragma once

#include "base/asset/asset_id.h"
#include "base/component/camera.h"
#include "base/core/scene_ops.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <glm/glm.hpp>

class AssetManager;
class SceneAsset;
struct SceneCameraConfig;

enum class SceneMode : uint8_t
{
    Mode3D = 0,
    Mode2D = 1,
};

class Scene
{
public:
    Scene();

    void set_mode(SceneMode mode)
    {
        mode_ = mode;
        sync_camera_projection();
    }

    void clear();

    void instantiate(const SceneAsset& asset, AssetManager& assets,
                     const SceneCameraConfig& app_camera);

    bool sync_to_asset(SceneAsset& asset) const;

    [[nodiscard]] entt::registry&       registry() { return registry_; }
    [[nodiscard]] const entt::registry& registry() const { return registry_; }

    [[nodiscard]] Camera&       camera();
    [[nodiscard]] const Camera& camera() const;

    [[nodiscard]] int mesh_count() const;

    [[nodiscard]] AssetId source_scene_id() const { return source_scene_id_; }
    void set_source_scene_id(AssetId id) { source_scene_id_ = id; }

    glm::vec3 clear_color_{0.0f, 0.0f, 0.0f};
    glm::vec3 ambient_{0.05f, 0.05f, 0.05f};

    SceneMode mode_ = SceneMode::Mode3D;
    glm::vec2 ortho_extent_{16.0f, 10.0f};

    [[nodiscard]] bool is_2d() const { return mode_ == SceneMode::Mode2D; }

    void sync_camera_projection()
    {
        Camera& cam      = camera();
        cam.orthographic = is_2d();
        cam.ortho_height = ortho_extent_.y;
    }

private:
    entt::entity ensure_camera_entity();

    entt::registry registry_;
    AssetId        source_scene_id_{kInvalidAssetId};
};
