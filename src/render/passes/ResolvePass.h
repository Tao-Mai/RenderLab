#pragma once
#include <slang-rhi.h>

#include "core/Macros.h"
#include "render/graph/RenderPass.h"

namespace Falcor
{
class FALCOR_API ResolvePass : public RenderPass
{
    FALCOR_PLUGIN_CLASS(ResolvePass, "ResolvePass", "Resolve a multi-sampled texture.");

    static ref<ResolvePass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties = {})
    {
        return make_ref<ResolvePass>(std::move(device));
    }
    ResolvePass(rhi::ComPtr<rhi::IDevice> device);

    void                               setFormat(rhi::Format format) { mFormat = format; }
    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void                               execute(RenderContext* context, const RenderData& renderData) override;

private:
    rhi::Format mFormat = rhi::Format::Undefined;
};
}  // namespace Falcor
