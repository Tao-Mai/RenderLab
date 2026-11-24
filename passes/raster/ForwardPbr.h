#pragma once
#include <array>
#include <slang-rhi.h>
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderManager.h"
struct RenderContext;
class ForwardPbrPipelines
{
public:
    void setup(
        rhi::IDevice&                device,
        rhi::Format                  colorFormat,
        const rhi::DepthStencilDesc& depth,
        bool                         blend,
        bool                         clearcoat
    );
    [[nodiscard]] rhi::IRenderPipeline*
    get(const RenderContext& context, const MaterialTable::MaterialInfo& material);

private:
    rhi::ColorTargetDesc                                            color;
    rhi::DepthStencilDesc                                           depth;
    rhi::ComPtr<rhi::IInputLayout>                                  input;
    using CullPipelines = std::array<rhi::ComPtr<rhi::IRenderPipeline>, 2>;
    std::array<std::array<CullPipelines, 4>, 2> pipelines;
    bool                                                            clearcoatEnabled = false;
};
