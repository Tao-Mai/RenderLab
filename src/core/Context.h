#pragma once

class AssetDescManager;
class AssetDataManager;
class ConfigManager;
class Editor;
class InputManager;
class Renderer;
class SceneManager;
class Window;

namespace ecs
{
class CameraSystem;
class FreeFlyMoveSystem;
class CharacterMoveSystem;
}

struct Context
{
    ConfigManager*            config              = nullptr;
    Window*                   window              = nullptr;
    InputManager*             inputManager        = nullptr;
    AssetDescManager*         assetDescManager    = nullptr;
    AssetDataManager*         assetDataManager    = nullptr;
    SceneManager*             sceneManager        = nullptr;
    ecs::CameraSystem*        cameraSystem        = nullptr;
    ecs::FreeFlyMoveSystem*   freeFlyMoveSystem   = nullptr;
    ecs::CharacterMoveSystem* characterMoveSystem = nullptr;
    Renderer*                 renderer            = nullptr;
    Editor*                   editor              = nullptr;
};

[[nodiscard]] Context& context();
