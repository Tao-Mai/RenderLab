#include "base/platform/paths.h"

namespace paths
{
std::filesystem::path project_root()
{
    return std::filesystem::path{PROJECT_ROOT};
}

std::filesystem::path assets_dir()
{
    return project_root() / "assets";
}

std::filesystem::path shaders_dir()
{
    return project_root() / "shaders";
}

std::filesystem::path config_dir()
{
    return project_root() / "config";
}

std::filesystem::path scene_config_dir()
{
    return config_dir() / "scenes";
}
}  // namespace paths
