#pragma once

class ShaderManager;
class DescriptorManager;
class PipelineManager;
class RenderResourceManager;
struct SceneInterface;

// Renderer owns these objects and manages their lifetime.
struct RenderContext
{
    ShaderManager*         shaderManager   = nullptr;
    DescriptorManager*     descriptors     = nullptr;
    PipelineManager*       pipelineManager = nullptr;
    RenderResourceManager* resources       = nullptr;
    SceneInterface*        sceneInterface  = nullptr;
};

[[nodiscard]] RenderContext& renderContext();
