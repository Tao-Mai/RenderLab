#include "base/core/scene.h"

Scene::Scene()
{
    Entity cam;
    cam.name = "MainCamera";
    cam.camera.emplace();
    entities_.push_back(std::move(cam));
}

Camera& Scene::camera()
{
    for (auto& e : entities_)
    {
        if (e.camera)
            return *e.camera;
    }
    Entity cam;
    cam.name = "MainCamera";
    cam.camera.emplace();
    entities_.push_back(std::move(cam));
    return *entities_.back().camera;
}

const Camera& Scene::camera() const
{
    for (const auto& e : entities_)
    {
        if (e.camera)
            return *e.camera;
    }
    static const Camera fallback;
    return fallback;
}

int Scene::mesh_count() const
{
    int n = 0;
    for (const auto& e : entities_)
    {
        if (e.has_mesh())
            ++n;
    }
    return n;
}
