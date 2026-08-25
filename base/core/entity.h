#pragma once

#include "base/component/camera.h"
#include "base/component/light.h"
#include "base/component/mesh_renderer.h"
#include "base/component/transform.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <utility>

// 实体 = 可选组件的组合。Scene 持有 Entity 列表，demo 自行按组件渲染。
class Entity
{
public:
    std::string name;
    bool        dynamic = false;

    Transform                   transform;
    std::optional<MeshRenderer> mesh_renderer;
    std::optional<Camera>       camera;
    std::optional<Light>        light;

    [[nodiscard]] bool      has_mesh() const { return mesh_renderer.has_value(); }
    [[nodiscard]] glm::mat4 model() const { return transform.to_model(); }

    void upload_gpu(bool dyn = false)
    {
        if (mesh_renderer)
            mesh_renderer->upload(dyn);
    }

    void draw() const
    {
        if (mesh_renderer)
            mesh_renderer->draw();
    }

    static Entity make_mesh(std::string name, Mesh mesh, Transform xform = {});
    static Entity sphere(float radius = 0.5f, const Transform& xform = {});
    static Entity plane(float width = 1.0f, float height = 1.0f, const Transform& xform = {});
    static Entity cuboid(float width, float height, float depth, const Transform& xform = {});
};
