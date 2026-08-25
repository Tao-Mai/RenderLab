#pragma once

#include "base/core/entity.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

enum class SceneMode : uint8_t
{
    Mode3D = 0,
    Mode2D = 1,
};

// 薄场景容器：Entity 列表。相机 / 灯 / 网格都是组件，由 Demo 自己渲染。
class Scene
{
public:
    Scene();

    void set_mode(SceneMode mode)
    {
        mode_ = mode;
        sync_camera_projection();
    }

    Entity& add(Entity entity)
    {
        if (entity.has_mesh())
            entity.upload_gpu(entity.dynamic);
        entities_.push_back(std::move(entity));
        return entities_.back();
    }

    Entity& add_mesh(const std::string& name, Mesh mesh, const glm::vec3& pos = {},
                     bool dynamic = false)
    {
        Entity e = Entity::make_mesh(name, std::move(mesh));
        e.transform.position = pos;
        e.dynamic            = dynamic;
        return add(std::move(e));
    }

    Entity& add_light(const Light& light, const std::string& name = {})
    {
        Entity e;
        e.name               = name.empty() ? ("light" + std::to_string(entities_.size())) : name;
        e.transform.position = light.position;
        e.light              = light;
        return add(std::move(e));
    }

    [[nodiscard]] const std::vector<Entity>& entities() const { return entities_; }
    [[nodiscard]] std::vector<Entity>&       entities() { return entities_; }

    // 主相机：第一个带 Camera 组件的实体；没有则自动创建一个。
    [[nodiscard]] Camera&       camera();
    [[nodiscard]] const Camera& camera() const;

    [[nodiscard]] int mesh_count() const;

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
    std::vector<Entity> entities_;
};
