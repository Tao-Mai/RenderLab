#include <slang-rhi/shader-cursor.h>

#include "render/GpuScene.h"
#include "render/graph/RenderPass.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingShader.h"

#include <limits>

namespace
{
constexpr char              kVBuffer[]   = "vbuffer";
constexpr auto              kRaygenEntry = "raygenMain";
constexpr auto              kMissEntry   = "missMain";
constexpr rhi::HitGroupDesc kHitGroup{"vbufferHitGroup", "closestHitMain", "anyHitMain"};
constexpr uint32_t          kPayloadSize     = 4 * sizeof(uint32_t);
constexpr uint32_t          kInvalidInstance = std::numeric_limits<uint32_t>::max();

const RenderPassReflection::Field kPorts[]{
    {
        .name = kVBuffer,
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::RGBA32Uint,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Output,
    },
};

class VBufferRT : public RenderPass
{
    RL_PLUGIN_CLASS(
        VBufferRT, "VBufferRT", .desc = "Traces primary visibility into a packed hit buffer.", .ports = kPorts
    );

    explicit VBufferRT(rhi::ComPtr<rhi::IDevice> device) : RenderPass(std::move(device)) {}

    static std::shared_ptr<VBufferRT> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        CHECK(properties.empty());
        return std::make_shared<VBufferRT>(std::move(device));
    }

    void compile(RenderContext* context, const CompileData&) override
    {
        if (pipeline || !checkRayTracingSupport(*mpDevice))
            return;

        auto* program = context->shaderManager->getProgram(
            "passes/raytracing/vbuffer/vbuffer.slang",
            {kRaygenEntry, kMissEntry, kHitGroup.closestHitEntryPoint, kHitGroup.anyHitEntryPoint}
        );
        const rhi::RayTracingPipelineDesc pipelineDesc{
            .program                 = program,
            .hitGroupCount           = 1,
            .hitGroups               = &kHitGroup,
            .maxRecursion            = 1,
            .maxRayPayloadSize       = kPayloadSize,
            .maxAttributeSizeInBytes = SceneAccelerationStructures::kTriangleAttributeSize,
        };
        rhiCheck(mpDevice->createRayTracingPipeline(pipelineDesc, pipeline.writeRef()));

        const rhi::ShaderTableDesc tableDesc{
            .rayGenShaderCount           = 1,
            .rayGenShaderEntryPointNames = &kRaygenEntry,
            .missShaderCount             = 1,
            .missShaderEntryPointNames   = &kMissEntry,
            .hitGroupCount               = 1,
            .hitGroupNames               = &kHitGroup.hitGroupName,
            .program                     = program,
        };
        rhiCheck(mpDevice->createShaderTable(tableDesc, shaderTable.writeRef()));
    }

    void execute(RenderContext* context, const RenderData& data) override
    {
        auto& commands                                 = *context->commands;
        auto* output                                   = data.getTexture(kVBuffer);
        data.getDictionary()[kRenderPassPrngDimension] = 0u;
        if (!mpScene)
        {
            uint32_t invalidHit[4]{kInvalidInstance, 0, 0, 0};
            commands.clearTextureUint(output, rhi::kEntireTexture, invalidHit);
            return;
        }

        auto* tlas = mpScene->prepareRayTracing(commands);

        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        auto* object = pass->bindPipeline(pipeline, shaderTable);
        CHECK(object);
        mpScene->bindRayTracingGeometryData(*object, tlas);
        const rhi::ShaderCursor root(object);
        rhiCheck(root[kVBuffer].setBinding(output));

        const auto size = output->getDesc().size;
        pass->dispatchRays(0, size.width, size.height, 1);
        pass->end();
    }

private:
    rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
    rhi::ComPtr<rhi::IShaderTable>        shaderTable;
};
}  // namespace

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, VBufferRT>();
}
