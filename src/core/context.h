#pragma once

#include "asset/asset_desc.h"

class AssetManager;
class Camera;
class ConfigManager;
class Editor;
class Renderer;
class Window;

struct Context
{
    ConfigManager* config = nullptr;
    Window* window = nullptr;
    AssetManager* assets = nullptr;
    SceneDesc* scene = nullptr;
    Camera* camera = nullptr;
    Renderer* renderer = nullptr;
    Editor* editor = nullptr;
};

[[nodiscard]] Context& context();
