#pragma once

#include <filesystem>
#include <string>

#include <glm/glm.hpp>

struct EditorCameraConfig
{
    float pan_speed   = 1.0f;   // RMB drag sensitivity multiplier
    float orbit_speed = 0.25f;  // MMB orbit degrees per pixel
    float fly_speed   = 1.5f;   // WASD/QE speed scale (* distance * dt)
    float zoom_speed  = 0.1f;   // scroll zoom factor
};

// 默认场景相机（app.yaml）；scenes/*.yaml 可覆盖
struct SceneCameraConfig
{
    glm::vec3 position{4.0f, 3.5f, 5.0f};
    glm::vec3 target{0.0f, 0.5f, 0.0f};
    float     fov = 45.0f;
};

struct AppConfig
{
    int         window_width  = 1440;
    int         window_height = 960;
    std::string window_title  = "RenderLab";
    std::string demo          = "shadow";
    EditorCameraConfig editor_camera;
    SceneCameraConfig  scene_camera;
};

AppConfig LoadAppConfig(const std::filesystem::path& path);
