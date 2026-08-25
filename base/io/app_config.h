#pragma once

#include <filesystem>
#include <string>

#include <glm/glm.hpp>

#include "base/asset/asset_id.h"

struct EditorCameraConfig
{
    float pan_speed   = 1.0f;
    float orbit_speed = 0.25f;
    float fly_speed   = 1.5f;
    float zoom_speed  = 0.1f;
};

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
    AssetId     scene_asset_id{kInvalidAssetId};
    EditorCameraConfig editor_camera;
    SceneCameraConfig  scene_camera;
};

AppConfig LoadAppConfig(const std::filesystem::path& path);
