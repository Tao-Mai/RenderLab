#include "render/impl/passes/vbuffer/VBufferPass.h"

#include "render/GpuScene.h"
#include "render/resources/ShaderLayout.h"

#include <limits>
#include <slang-rhi/shader-cursor.h>
#include <utility>

namespace
{
constexpr char kHit[] = "hit";
constexpr char kHitImage[] = "hitImage";
constexpr auto kHitFormat = rhi::Format::RGBA32Uint;
constexpr uint32_t kHitPayloadSize = 4 * sizeof(uint32_t);
constexpr uint32_t kMaxRayRecursion = 1;
constexpr uint32_t kInvalidInstance = std::numeric_limits<uint32_t>::max();

const RenderPassReflection::Field kPorts[]{
    {
        .name = kHit,
        .resource = rhi::TextureDesc{.format = kHitFormat, .usage = rhi::TextureUsage::UnorderedAccess},
        .visibility = RenderPassReflection::Visibility::Output,
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
};

void validateShaderLayout(rhi::IShaderProgram& program)
{
    const ShaderLayout layout(program);
    CHECK(layout.storageImageFormat(kHitImage) == kHitFormat, "storage image format differs for '{}'", kHitImage);
}
}  // namespace

VBufferPass::VBufferPass(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(std::move(device), {.type = kType, .ports = kPorts}),
    program(
        *mpDevice,
        {
            .source = "passes/vbuffer/vbuffer.slang",
            .maxRecursion = kMaxRayRecursion,
            .maxRayPayloadSize = kHitPayloadSize,
            .maxAttributeSizeInBytes = SceneAccelerationStructures::kTriangleAttributeSize,
            .validateLayout = validateShaderLayout,
            .useVisibilityRays = false,
        },
        {}
    )
{
}

void VBufferPass::execute(RenderContext* context, const RenderData& renderData)
{
    auto& commands = *context->commands;
    auto* output = renderData.getTexture(kHit);
    if (!mpScene)
    {
        uint32_t miss[4]{kInvalidInstance, 0, 0, 0};
        commands.clearTextureUint(output, rhi::kEntireTexture, miss);

        return;
    }

    const auto& version = program.getActiveVersion(*context->shaderManager);
    auto* tlas = mpScene->prepareRayTracing(commands);
    mpScene->bindRayTracingGeometryData(*version.shaderObject, tlas);
    rhiCheck(rhi::ShaderCursor(version.shaderObject)[kHitImage].setBinding(output));

    const auto extent = output->getDesc().size;
    auto* pass = commands.beginRayTracingPass();
    CHECK(pass);
    pass->bindPipeline(version.pipeline, version.shaderTable, version.shaderObject);
    pass->dispatchRays(0, extent.width, extent.height, extent.depth);
    pass->end();
}
