#include "base/core/entity.h"

Entity Entity::make_mesh(std::string name, Mesh mesh, Transform xform)
{
    Entity e;
    e.name      = std::move(name);
    e.transform = xform;
    e.mesh_renderer.emplace();
    e.mesh_renderer->mesh = std::move(mesh);
    return e;
}

Entity Entity::sphere(float radius, const Transform& xform)
{
    return make_mesh("sphere", MeshFactory::createSphere(radius), xform);
}

Entity Entity::plane(float width, float height, const Transform& xform)
{
    return make_mesh("plane", MeshFactory::createPlane(width, height), xform);
}

Entity Entity::cuboid(float width, float height, float depth, const Transform& xform)
{
    return make_mesh("cuboid", MeshFactory::createCuboid(width, height, depth), xform);
}
