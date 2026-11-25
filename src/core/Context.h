#pragma once

#include "core/Config.h"

class AssetManager;
class Editor;
class Renderer;
class SceneManager;
class Window;

struct Context
{
    Config config;

    Window*                   window              = nullptr;
    AssetManager*            assetManager        = nullptr;
    SceneManager*             sceneManager        = nullptr;
    Renderer*                 renderer            = nullptr;
    Editor*                   editor              = nullptr;
};

[[nodiscard]] Context& context();
