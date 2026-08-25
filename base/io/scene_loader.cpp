#include "base/io/scene_loader.h"

#include "base/core/entity.h"

#include <glog/logging.h>
#include <yaml-cpp/yaml.h>

#include <fstream>

#include <glm/gtc/quaternion.hpp>

namespace
{
glm::vec3 read_vec3(const YAML::Node& node, const glm::vec3& fallback = {})
{
    if (!node || !node.IsSequence() || node.size() < 3)
        return fallback;
    return {node[0].as<float>(), node[1].as<float>(), node[2].as<float>()};
}

YAML::Node write_vec3(const glm::vec3& v)
{
    YAML::Node n;
    n.push_back(v.x);
    n.push_back(v.y);
    n.push_back(v.z);
    return n;
}

Transform read_transform(const YAML::Node& node)
{
    Transform t;
    if (!node)
        return t;
    t.position = read_vec3(node["position"], t.position);
    t.scale    = read_vec3(node["scale"], t.scale);
    if (const auto rot = node["rotation"])
    {
        const glm::vec3 euler_deg = read_vec3(rot);
        t.rotation                = glm::quat(glm::radians(euler_deg));
    }
    return t;
}

void write_transform(YAML::Node node, const Transform& t)
{
    node["position"]          = write_vec3(t.position);
    node["scale"]             = write_vec3(t.scale);
    const glm::vec3 euler_deg = glm::degrees(glm::eulerAngles(t.rotation));
    node["rotation"]          = write_vec3(euler_deg);
}

void apply_material(MeshRenderer& mr, const YAML::Node& mat)
{
    if (!mat)
        return;
    if (mat["albedo"])
        mr.material.albedo = read_vec3(mat["albedo"], mr.material.albedo);
    if (mat["specular"])
        mr.material.specular = read_vec3(mat["specular"], mr.material.specular);
    if (mat["shininess"])
        mr.material.shininess = mat["shininess"].as<float>();
}
}  // namespace

void ApplySceneCamera(Scene& scene, const SceneCameraConfig& cam)
{
    scene.camera().look_at(cam.position, cam.target);
    scene.camera().fov = cam.fov;
}

void LoadScene(const std::filesystem::path& path, Scene& scene, AssetCache& assets,
               const SceneCameraConfig& app_camera)
{
    CHECK(std::filesystem::exists(path)) << "Scene config not found: " << path;

    const YAML::Node root = YAML::LoadFile(path.string());

    scene.set_mode(SceneMode::Mode3D);
    scene.entities().clear();

    if (root["clear_color"])
        scene.clear_color_ = read_vec3(root["clear_color"], scene.clear_color_);
    if (root["ambient"])
        scene.ambient_ = read_vec3(root["ambient"], scene.ambient_);

    ApplySceneCamera(scene, app_camera);
    if (const auto cam = root["camera"])
    {
        SceneCameraConfig override_cam = app_camera;
        if (cam["position"])
            override_cam.position = read_vec3(cam["position"], override_cam.position);
        if (cam["target"])
            override_cam.target = read_vec3(cam["target"], override_cam.target);
        if (cam["fov"])
            override_cam.fov = cam["fov"].as<float>();
        ApplySceneCamera(scene, override_cam);
    }

    if (const auto lights = root["lights"])
    {
        int light_i = 0;
        for (const auto& light_node : lights)
        {
            const std::string type =
                light_node["type"] ? light_node["type"].as<std::string>() : "point";
            if (type != "point")
            {
                LOG(WARNING) << "Unsupported light type '" << type << "', skipped";
                continue;
            }
            const glm::vec3 pos   = read_vec3(light_node["position"], {2.0f, 3.0f, 2.0f});
            const glm::vec3 color = read_vec3(light_node["color"], {1.0f, 1.0f, 1.0f});
            const float intensity =
                light_node["intensity"] ? light_node["intensity"].as<float>() : 1.0f;
            Light light = Light::point(pos, color, intensity);
            if (light_node["constant"])
                light.constant = light_node["constant"].as<float>();
            if (light_node["linear"])
                light.linear = light_node["linear"].as<float>();
            if (light_node["quadratic"])
                light.quadratic = light_node["quadratic"].as<float>();
            scene.add_light(light, "light" + std::to_string(light_i++));
        }
    }

    if (const auto objects = root["objects"])
    {
        for (const auto& obj_node : objects)
        {
            const std::string name =
                obj_node["name"] ? obj_node["name"].as<std::string>() : "object";
            const std::string mesh_key =
                obj_node["mesh"] ? obj_node["mesh"].as<std::string>() : "builtin:cube";

            Entity    entity = Entity::make_mesh(name, assets.get_mesh(mesh_key));
            Transform xform  = read_transform(obj_node["transform"]);
            entity.transform = xform;
            apply_material(*entity.mesh_renderer, obj_node["material"]);
            scene.add(std::move(entity));
        }
    }

    LOG(INFO) << "Loaded scene '" << path.filename().string()
              << "' meshes=" << scene.mesh_count() << " entities=" << scene.entities().size();
}

bool SaveScene(const std::filesystem::path& path, const Scene& scene)
{
    if (!std::filesystem::exists(path))
    {
        LOG(ERROR) << "Cannot save scene, file missing: " << path;
        return false;
    }

    YAML::Node root = YAML::LoadFile(path.string());

    {
        YAML::Node cam  = root["camera"] ? root["camera"] : YAML::Node(YAML::NodeType::Map);
        cam["position"] = write_vec3(scene.camera().position);
        cam["target"]   = write_vec3(scene.camera().position + scene.camera().front);
        cam["fov"]      = scene.camera().fov;
        root["camera"]  = cam;
    }

    if (root["lights"] && root["lights"].IsSequence())
    {
        std::size_t light_i = 0;
        for (const auto& e : scene.entities())
        {
            if (!e.light)
                continue;
            if (light_i >= root["lights"].size())
                break;
            root["lights"][light_i]["position"]  = write_vec3(e.light->position);
            root["lights"][light_i]["color"]     = write_vec3(e.light->color);
            root["lights"][light_i]["intensity"] = e.light->intensity;
            ++light_i;
        }
    }

    if (root["objects"] && root["objects"].IsSequence())
    {
        for (auto obj_node : root["objects"])
        {
            if (!obj_node["name"])
                continue;
            const std::string name = obj_node["name"].as<std::string>();
            for (const auto& e : scene.entities())
            {
                if (!e.has_mesh() || e.name != name)
                    continue;
                write_transform(obj_node["transform"], e.transform);
                break;
            }
        }
    }

    std::ofstream out(path);
    if (!out)
    {
        LOG(ERROR) << "Failed to open scene for write: " << path;
        return false;
    }
    out << root;
    LOG(INFO) << "Saved scene: " << path;
    return true;
}
