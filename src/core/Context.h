#pragma once

class AssetDescManager;
class AssetDataManager;
class Camera;
class ConfigManager;
class SceneDesc;
class Editor;
class Renderer;
class Window;

struct Context
{
    ConfigManager*    config           = nullptr;
    Window*           window           = nullptr;
    AssetDescManager* assetDescManager = nullptr;
    AssetDataManager* assetDataManager = nullptr;
    SceneDesc*        scene            = nullptr;
    Camera*           camera           = nullptr;
    Renderer*         renderer         = nullptr;
    Editor*           editor           = nullptr;
};

[[nodiscard]] Context& context();
