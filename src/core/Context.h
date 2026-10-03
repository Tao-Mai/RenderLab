#pragma once

class AssetManager;
class AssetDataManager;
class ConfigManager;
class Editor;
class InputManager;
class Renderer;
class SceneManager;
class Window;

struct Context
{
    ConfigManager*            config              = nullptr;
    Window*                   window              = nullptr;
    InputManager*             inputManager        = nullptr;
    AssetManager*            assetManager        = nullptr;
    AssetDataManager*         assetDataManager    = nullptr;
    SceneManager*             sceneManager        = nullptr;
    Renderer*                 renderer            = nullptr;
    Editor*                   editor              = nullptr;
};

[[nodiscard]] Context& context();
