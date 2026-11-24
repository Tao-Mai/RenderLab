#include "render/raster/ShadowResources.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/BuiltinAssets.h"
#include "render/device/RhiCheck.h"
#include "render/graph/RenderPass.h"
#include "render/resources/RenderResourceManager.h"

void addShadowInputs(RenderPassReflection& reflection)
{
    reflection.addInput(kShadowUniformsPort, rhi::BufferDesc{
        .size = sizeof(ShadowUniforms),
        .usage = rhi::BufferUsage::ShaderResource,
    });
    for (const auto& ports : kShadowPorts)
    {
        reflection.addInput(ports.map, rhi::TextureDesc{
            .type = rhi::TextureType::Texture2DArray,
            .size = {0, 0, 1},
            .arrayLength = 0,
            .format = rhi::Format::D32Float,
            .usage = rhi::TextureUsage::ShaderResource,
        });
        reflection.addInput(ports.data, rhi::BufferDesc{.usage = rhi::BufferUsage::ShaderResource});
    }
}

ShadowBindings::ShadowBindings(const RenderData& data, RenderResourceManager& resources) :
    uniforms(data.getBuffer(kShadowUniformsPort)),
    compareNearest(resources.sampler(BuiltinAssets::Sampler::kShadowNearest)),
    compareLinear(resources.sampler(BuiltinAssets::Sampler::kShadowLinear)),
    linearSampler(resources.sampler(BuiltinAssets::Sampler::linearClamp))
{
    for (size_t index = 0; index < kShadowPorts.size(); ++index)
    {
        const auto& ports = kShadowPorts[index];
        maps[index]       = data.getTexture(ports.map);
        buffers[index]    = data.getBuffer(ports.data);
    }
}

void ShadowBindings::bind(const rhi::ShaderCursor& root) const
{
    rhiCheck(root[kShadowUniformsPort].setBinding(uniforms));
    rhiCheck(root["shadowNearestComparisonSampler"].setBinding(compareNearest));
    rhiCheck(root["shadowLinearComparisonSampler"].setBinding(compareLinear));
    rhiCheck(root["shadowLinearSampler"].setBinding(linearSampler));
    for (size_t index = 0; index < kShadowPorts.size(); ++index)
    {
        const auto& ports = kShadowPorts[index];
        rhiCheck(root[ports.map].setBinding(maps[index]));
        rhiCheck(root[ports.data].setBinding(buffers[index]));
    }
}
