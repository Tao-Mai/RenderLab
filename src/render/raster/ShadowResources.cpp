#include "render/raster/ShadowResources.h"
#include "asset/BuiltinAssets.h"
#include "render/FrameData.h"
#include "render/device/RhiCheck.h"
#include "render/graph/RenderPass.h"
#include "render/resources/RenderResourceManager.h"

void addShadowInputs(Falcor::RenderPassReflection& reflection)
{
    for (const auto& ports : kShadowPorts)
    {
        reflection.addInput(ports.map, "Shadow depth array")
            .texture2D(0, 0, 1, 1, 0)
            .format(rhi::Format::D32Float)
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource);
        reflection.addInput(ports.data, "Shadow transforms")
            .rawBuffer(0)
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    }
}

ShadowBindings::ShadowBindings(const Falcor::RenderData& data, const FrameData& frame, RenderResourceManager& resources) :
    frame(frame), linearSampler(resources.sampler(BuiltinAssets::Sampler::linearClamp))
{
    for (size_t index = 0; index < kShadowPorts.size(); ++index)
    {
        const auto& ports = kShadowPorts[index];
        maps[index]       = data.getTexture(ports.map)->getDefaultView();
        CHECK(maps[index]);
        buffers[index] = data.getBuffer(ports.data);
    }
}

void ShadowBindings::bind(rhi::IShaderObject* object) const
{
    const rhi::ShaderCursor root(object);
    rhiCheck(root["shadowUniforms"].getDereferenced().setData(frame.shadowUniforms));
    rhiCheck(root["shadowSampler"].setBinding(frame.shadowSampler));
    rhiCheck(root["shadowLinearSampler"].setBinding(linearSampler));
    for (size_t index = 0; index < kShadowPorts.size(); ++index)
    {
        const auto& ports = kShadowPorts[index];
        rhiCheck(root[ports.map].setBinding(maps[index]));
        rhiCheck(root[ports.data].setBinding(buffers[index]));
    }
}
