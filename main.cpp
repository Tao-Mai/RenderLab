#include "base/core/demo.h"
#include "base/asset/asset_id.h"
#include "base/asset/asset_manager.h"
#include "base/core/reflection/register_reflection.h"
#include "base/io/app_config.h"
#include "base/platform/paths.h"
#include "base/platform/window.h"
#include "base/editor/editor.h"

#include <glog/logging.h>

int main(int argc, char* argv[])
{
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    if (!glfwInit())
    {
        LOG(ERROR) << "Failed to init GLFW";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    reflection::register_all();

    AssetManager assets;
    assets.init();

    const auto      cfg_path = paths::config_dir() / "app.yaml";
    const AppConfig cfg      = LoadAppConfig(cfg_path);
    LOG(INFO) << "RenderLab init, scene=" << format_asset_id(cfg.scene_asset_id);

    GlfwWindow window(cfg.window_width, cfg.window_height, cfg.window_title,
                      GlfwWindow::WindowState::Normal);

    DemoRegistry registry;
    Editor       editor(window, registry, assets, cfg);
    editor.run();

    LOG(INFO) << "RenderLab shutdown";
    glfwTerminate();
    return 0;
}
