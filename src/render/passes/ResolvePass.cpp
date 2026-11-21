#include "render/passes/ResolvePass.h"

#include "render/resources/Texture.h"

#include <utility>

namespace Falcor
{
namespace
{
const std::string kSrc = "src";
const std::string kDst = "dst";
}  // namespace

ResolvePass::ResolvePass(ref<Device> device) : RenderPass(std::move(device)) {}

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
    context->resolveResource(renderData.getTexture(kSrc), renderData.getTexture(kDst));
}
}  // namespace Falcor
