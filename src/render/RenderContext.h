#pragma once

class ShaderManager;
class Device;
class RenderResourceManager;
struct SceneInterface;
struct EditorLayouts;

// Renderer owns these objects and manages their lifetime.
struct RenderContext
{
    ShaderManager*         shaderManager  = nullptr;
    Device*                device         = nullptr;
    RenderResourceManager* resources      = nullptr;
    SceneInterface*        sceneInterface = nullptr;
    EditorLayouts*         editorLayouts  = nullptr;
};

[[nodiscard]] RenderContext& renderContext();
