#include "base/io/app_config.h"

#include <glog/logging.h>
#include <yaml-cpp/yaml.h>

namespace
{
glm::vec3 read_vec3(const YAML::Node& node, const glm::vec3& fallback)
{
    if (!node || !node.IsSequence() || node.size() < 3)
        return fallback;
    return {node[0].as<float>(), node[1].as<float>(), node[2].as<float>()};
}
}  // namespace

AppConfig LoadAppConfig(const std::filesystem::path& path)
{
    CHECK(std::filesystem::exists(path)) << "App config not found: " << path;

    const YAML::Node root = YAML::LoadFile(path.string());
    AppConfig        cfg;

    if (const auto window = root["window"])
    {
        if (window["width"])
            cfg.window_width = window["width"].as<int>();
        if (window["height"])
            cfg.window_height = window["height"].as<int>();
        if (window["title"])
            cfg.window_title = window["title"].as<std::string>();
    }

    if (root["demo"])
        cfg.demo = root["demo"].as<std::string>();

    if (const auto cam = root["camera"])
    {
        if (cam["position"])
            cfg.scene_camera.position = read_vec3(cam["position"], cfg.scene_camera.position);
        if (cam["target"])
            cfg.scene_camera.target = read_vec3(cam["target"], cfg.scene_camera.target);
        if (cam["fov"])
            cfg.scene_camera.fov = cam["fov"].as<float>();
    }

    if (const auto editor = root["editor"])
    {
        if (const auto cam = editor["camera"])
        {
            if (cam["pan_speed"])
                cfg.editor_camera.pan_speed = cam["pan_speed"].as<float>();
            if (cam["orbit_speed"])
                cfg.editor_camera.orbit_speed = cam["orbit_speed"].as<float>();
            if (cam["fly_speed"])
                cfg.editor_camera.fly_speed = cam["fly_speed"].as<float>();
            if (cam["zoom_speed"])
                cfg.editor_camera.zoom_speed = cam["zoom_speed"].as<float>();
        }
    }

    return cfg;
}
