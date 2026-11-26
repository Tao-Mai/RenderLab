#include "render/impl/passes/accumulate/AccumulatePass.h"

#include "render/DispatchGroups.h"
#include "render/graph/RenderPassStandardFlags.h"

#include <slang-rhi/shader-cursor.h>
#include <utility>

namespace
{
constexpr char kSource[] = "source";
constexpr char kColor[] = "color";
constexpr char kShaderFile[] = "passes/accumulate/accumulate.slang";
constexpr auto kOutputFormat = rhi::Format::RGBA32Float;

const RenderPassReflection::Field kPorts[]{
    {
        .name = kSource,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource},
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {
        .name = kColor,
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = kOutputFormat,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Output,
        .matchInput = kSource,
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
};
}  // namespace

AccumulatePass::AccumulatePass(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(std::move(device), {.type = kType, .ports = kPorts})
{
}

void AccumulatePass::compile(RenderContext* context, const CompileData&)
{
    rhi::FormatSupport support;
    rhiCheck(mpDevice->getFormatSupport(kOutputFormat, &support));
    CHECK(rhi::is_set(support, rhi::FormatSupport::ShaderUavLoad) &&
              rhi::is_set(support, rhi::FormatSupport::ShaderUavStore),
          "AccumulatePass requires RGBA32Float storage reads and writes");

    rhiCheck(mpDevice->createComputePipeline(
        {.program = context->shaderManager->getProgram(kShaderFile, {"computeMain"})}, pipeline.writeRef()
    ));
    sampleCount = 0;
}

void AccumulatePass::setScene(GpuScene* scene)
{
    RenderPass::setScene(scene);
    sampleCount = 0;
}

void AccumulatePass::onSceneUpdates(RenderContext*, SceneUpdateFlags) { sampleCount = 0; }

void AccumulatePass::execute(RenderContext* context, const RenderData& renderData)
{
    const auto& dictionary = renderData.getDictionary();
    const auto refresh = dictionary.find(kRenderPassRefreshFlags);
    if (refresh != dictionary.end() && refresh->second != std::to_underlying(RenderPassRefreshFlags::None))
        sampleCount = 0;

    const float sampleWeight = 1.0f / static_cast<float>(++sampleCount);
    auto* output = renderData.getTexture(kColor);
    const auto groups = dispatchGroups(pipeline, output->getDesc().size);

    auto* pass = context->commands->beginComputePass();
    CHECK(pass);
    
    auto* object = pass->bindPipeline(pipeline);
    CHECK(object);

    const rhi::ShaderCursor root(object);
    rhiCheck(root["sourceColor"].setBinding(renderData.getTexture(kSource)));
    rhiCheck(root["accumulatedColor"].setBinding(output));
    rhiCheck(root["sampleWeight"].setData(sampleWeight));
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}
