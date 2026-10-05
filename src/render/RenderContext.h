#pragma once

class ShaderManager;
class DescriptorManager;
class PipelineManager;
class RenderResourceManager;

// Renderer owns these objects and manages their lifetime.
struct RenderContext
{
    ShaderManager*         shaderManager   = nullptr;
    DescriptorManager*     descriptors     = nullptr;
    PipelineManager*       pipelineManager = nullptr;
    RenderResourceManager* resources       = nullptr;
};

[[nodiscard]] RenderContext& renderContext();
