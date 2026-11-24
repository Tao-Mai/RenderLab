#include <cstdint>
#include <optional>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/device/RhiCheck.h"
#include "render/graph/RenderPass.h"
#include "render/graph/RenderPassStandardFlags.h"

namespace
{
constexpr char kInput[]  = "input";
constexpr char kOutput[] = "output";

struct alignas(16) AccumulatedPixel
{
    float    radiance[3];
    uint32_t sampleCount;
};
static_assert(sizeof(AccumulatedPixel) == 16);

class AccumulatePass : public RenderPass
{
    RL_PLUGIN_CLASS(AccumulatePass, "AccumulatePass", "Accumulates radiance samples across frames.");

    explicit AccumulatePass(rhi::ComPtr<rhi::IDevice> device) : RenderPass(std::move(device), 1) {}

    static ref<AccumulatePass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        CHECK(properties.empty());
        return make_ref<AccumulatePass>(std::move(device));
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(RenderContext* context, const CompileData& compileData) override;
    void                               execute(RenderContext* context, const RenderData& renderData) override;

private:
    rhi::ComPtr<rhi::IComputePipeline> pipeline;
    rhi::ComPtr<rhi::IBuffer>          history;
    std::optional<uint64_t>            previousRevision;
};

RenderPassReflection AccumulatePass::reflect(const CompileData& compileData)
{
    const auto*          input = compileData.connectedResources.getField(kInput);
    RenderPassReflection reflection;
    reflection.addInput(kInput, rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource});
    reflection.addOutput(kOutput, rhi::TextureDesc{
        .size = {input ? input->texture().size.width : 0, input ? input->texture().size.height : 0, 1},
        .format = rhi::Format::RGBA16Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    return reflection;
}

void AccumulatePass::compile(RenderContext* context, const CompileData& compileData)
{
    const auto& input  = *compileData.connectedResources.getField(kInput);
    const auto  width  = input.texture().size.width ? input.texture().size.width : compileData.defaultTexDims.x;
    const auto  height = input.texture().size.height ? input.texture().size.height : compileData.defaultTexDims.y;

    rhi::BufferDesc desc;
    desc.size = size_t(width) * height * sizeof(AccumulatedPixel);
    desc.usage =
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
    rhiCheck(mpDevice->createBuffer(desc, nullptr, history.writeRef()));

    auto& shaders = *context->shaderManager;
    rhiCheck(mpDevice->createComputePipeline(
        {.program = shaders.getProgram("passes/postprocess/AccumulatePass/accumulate.slang", {"computeMain"})},
        pipeline.writeRef()
    ));
    previousRevision.reset();
}

void AccumulatePass::execute(RenderContext* context, const RenderData& renderData)
{
    auto& commands = *context->commands;

    const auto& dictionary = renderData.getDictionary();
    const auto refresh = dictionary.find(kRenderPassRefreshFlags);
    const bool refreshRequested = refresh != dictionary.end() && refresh->second != 0;
    const auto revision = renderData.getHistoryRevision(kInput);
    if (refreshRequested || previousRevision != revision)
    {
        commands.clearBuffer(history);
        previousRevision = revision;
        renderData.invalidateHistory(kOutput);
    }

    const auto output = renderData.getTexture(kOutput);
    auto*      pass   = commands.beginComputePass();
    CHECK(pass);
    auto* object = pass->bindPipeline(pipeline);
    CHECK(object);
    const rhi::ShaderCursor root(object);
    rhiCheck(root["inputColor"].setBinding(renderData.getTexture(kInput)));
    rhiCheck(root["outputColor"].setBinding(output));
    rhiCheck(root["historyBuffer"].setBinding(history));

    const auto& size   = output->getDesc().size;
    const auto  groups = dispatchGroups(pipeline, {size.width, size.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}
}  // namespace

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, AccumulatePass>();
}
