#pragma once

#include "base/gfx/asset_cache.h"
#include "base/io/app_config.h"
#include "base/core/scene.h"

#include <filesystem>

void ApplySceneCamera(Scene& scene, const SceneCameraConfig& cam);

// 从 YAML 填充 scene。相机：先应用 app_camera，若 yaml 含 camera 再覆盖。
void LoadScene(const std::filesystem::path& path, Scene& scene, AssetCache& assets,
               const SceneCameraConfig& app_camera);

// 把当前 entity transform（及相机、灯光）写回 YAML，保留 mesh / material 等其它字段。
bool SaveScene(const std::filesystem::path& path, const Scene& scene);
