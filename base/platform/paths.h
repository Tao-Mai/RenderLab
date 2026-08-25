#pragma once

#include <filesystem>

namespace paths
{
std::filesystem::path project_root();
std::filesystem::path assets_dir();
std::filesystem::path shaders_dir();
std::filesystem::path config_dir();
std::filesystem::path scene_config_dir();
}
