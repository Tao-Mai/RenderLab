#pragma once

#include "core/Macros.h"
#include "render/graph/RenderPass.h"

namespace Falcor
{
class FALCOR_API ResolvePass : public RenderPass
{
    FALCOR_PLUGIN_CLASS(ResolvePass, "ResolvePass", "Resolve a multi-sampled texture.");

    static ref<ResolvePass> create(ref<Device> device, const Properties& properties = {})
    {
        return make_ref<ResolvePass>(std::move(device));
    }
    ResolvePass(ref<Device> device);

    void                               setFormat(ResourceFormat format) { mFormat = format; }
    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void                               execute(RenderContext* context, const RenderData& renderData) override;

private:
    ResourceFormat mFormat = ResourceFormat::Unknown;
};
}  // namespace Falcor
