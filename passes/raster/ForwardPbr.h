#pragma once
#include <array>
#include <slang-rhi.h>
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderManager.h"
namespace Falcor
{
struct RenderContext;
}
class ForwardPbrPipelines
{
public:
    void setup(
        rhi::IDevice&                device,
        rhi::Format                  colorFormat,
        const rhi::DepthStencilDesc& depth,
        bool                         blend,
        bool                         ibl,
        bool                         clearcoat
    );
    [[nodiscard]] rhi::IRenderPipeline*
    get(const Falcor::RenderContext& context, const MaterialTable::MaterialInfo& material);

private:
    rhi::ColorTargetDesc                                            color;
    rhi::DepthStencilDesc                                           depth;
    rhi::ComPtr<rhi::IInputLayout>                                  input;
    std::array<std::array<rhi::ComPtr<rhi::IRenderPipeline>, 2>, 4> pipelines;
    bool                                                            iblEnabled       = false;
    bool                                                            clearcoatEnabled = false;
};
