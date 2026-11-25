#pragma once

#include "core/Context.h"
#include "core/Window.h"
#include "core/utils/NonCopyable.h"

#include <filesystem>
#include <memory>

class Engine : private NonCopyable
{
public:
    Engine();
    ~Engine();

    static void loadConfig();
    [[nodiscard]] static std::filesystem::path assetRoot();

    void init();
    void run();
    void shutdown() noexcept;

private:
    Window                            window;
    std::unique_ptr<AssetManager>     assetManager;
    std::unique_ptr<SceneManager>     sceneManager;
    std::unique_ptr<Renderer>         renderer;
    std::unique_ptr<Editor>           editor;

    void initWindow();
    void mainLoop();
};
