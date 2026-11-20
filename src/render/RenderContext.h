#pragma once

class ShaderManager;
class Device;
class RenderResourceManager;
class MaterialTable;
class OfflineTracePipelineCache;

// Renderer owns these objects and manages their lifetime.
struct RenderContext
{
    ShaderManager*             shaderManager    = nullptr;
    Device*                    device           = nullptr;
    RenderResourceManager*     resources        = nullptr;
    MaterialTable*             materialTable    = nullptr;
    OfflineTracePipelineCache* offlinePipelines = nullptr;
};

[[nodiscard]] RenderContext& renderContext();
