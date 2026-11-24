#pragma once

#include <array>
#include <slang-rhi.h>
#include "render/resources/MaterialTable.h"

class GpuScene;
class ShadowBindings;
struct RenderContext;

namespace rhi
{
struct ShaderCursor;
}

// Bind the texture fields declared by the scene_lighting.slang variants.
void bindPbrTextures(const rhi::ShaderCursor& root, const GpuScene& scene, bool enableSheen);

class ForwardPbrPipelines
{
public:
    void setup(
        rhi::IDevice& device, rhi::Format colorFormat, const rhi::DepthStencilDesc& depth, bool blend, bool clearcoat
    );
    [[nodiscard]] rhi::IShaderObject& bind(
        rhi::IRenderPassEncoder&           pass,
        const RenderContext&               context,
        const MaterialTable::MaterialInfo& material,
        const ShadowBindings&              shadows,
        float                              ambientCoefficient
    );

private:
    rhi::ColorTargetDesc           color;
    rhi::DepthStencilDesc          depth;
    rhi::ComPtr<rhi::IInputLayout> input;
    using CullPipelines = std::array<rhi::ComPtr<rhi::IRenderPipeline>, 2>;
    std::array<std::array<CullPipelines, 4>, 2> pipelines;
    bool                                        clearcoatEnabled = false;
};
