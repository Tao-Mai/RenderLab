#include "render/passes/ResolvePass.h"

#include <slang-rhi.h>

#include <utility>

namespace Falcor
{
namespace
{
    const std::string kSrc = "src";
    const std::string kDst = "dst";
}  // namespace

ResolvePass::ResolvePass(rhi::ComPtr<rhi::IDevice> device) : RenderPass(std::move(device)) {}

RenderPassReflection ResolvePass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    reflection.addInput(kSrc, "Multisampled texture").format(mFormat).texture2D(0, 0, 0, 1, 0);

    const auto* source = compileData.connectedResources.getField(kSrc);
    auto&       target = reflection.addOutput(kDst, "Single-sampled texture").format(mFormat);
    if (source)
        target.texture2D(source->getWidth(), source->getHeight(), 1, 1, source->getArraySize());
    return reflection;
}

void ResolvePass::execute(RenderContext* context, const RenderData& renderData)
{
    const auto source = renderData.getTexture(kSrc);
    const auto target = renderData.getTexture(kDst);
    for (uint32_t layer = 0; layer < source->getDesc().getLayerCount(); ++layer)
    {
        const auto sourceView =
            source->createView({.subresourceRange = {.layer = layer, .layerCount = 1, .mip = 0, .mipCount = 1}});
        const auto targetView =
            target->createView({.subresourceRange = {.layer = layer, .layerCount = 1, .mip = 0, .mipCount = 1}});
        CHECK(sourceView && targetView);
        rhi::RenderPassColorAttachment color;
        color.view          = sourceView;
        color.resolveTarget = targetView;
        color.loadOp        = rhi::LoadOp::Load;
        auto* pass          = context->commands->beginRenderPass({&color, 1});
        CHECK(pass);
        pass->end();
    }
}
}  // namespace Falcor
