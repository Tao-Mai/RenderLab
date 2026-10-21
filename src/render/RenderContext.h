#pragma once

class ShaderManager;
class Device;
class RenderResourceManager;

// Renderer owns these objects and manages their lifetime.
struct RenderContext
{
    ShaderManager*         shaderManager = nullptr;
    Device*                device        = nullptr;
    RenderResourceManager* resources     = nullptr;
};

[[nodiscard]] RenderContext& renderContext();
