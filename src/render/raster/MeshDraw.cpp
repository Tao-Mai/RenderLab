#include "render/raster/MeshDraw.h"

#include "render/FrameData.h"
#include "render/resources/GpuMesh.h"

MeshCullPipelines createMeshCullPipelines(Falcor::Device& device, GraphicsPipelineInfo info)
{
    info.state.cullMode    = vk::CullModeFlagBits::eBack;
    const auto singleSided = device.getOrCreatePipeline(info);
    info.state.cullMode    = vk::CullModeFlagBits::eNone;
    return {singleSided, device.getOrCreatePipeline(info)};
}

void bindMeshGeometry(vk::raii::CommandBuffer& commands, const Falcor::RenderContext& frame)
{
    const auto layout = frame.getDevice()->meshPipelineLayout;
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        layout,
        RenderInterface::kSceneSet,
        frame.frameData->sceneSetHandle(),
        {}
    );
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        layout,
        RenderInterface::kMaterialSet,
        frame.scene->materialSetHandle(),
        {}
    );
}

void MeshDrawRecorder::draw(const DrawItem& item, uint32_t materialIndex, vk::Pipeline pipeline)
{
    if (pipeline != boundPipeline)
    {
        commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        boundPipeline = pipeline;
    }

    const auto& mesh = resources.mesh(item.mesh);
    if (&mesh != boundMesh)
    {
        mesh.bind(commands);
        boundMesh = &mesh;
    }

    const MeshPushConstants pushConstants{.model = item.model, .materialIndex = materialIndex};
    commands.pushConstants<MeshPushConstants>(
        layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        pushConstants
    );
    const auto& submesh = mesh.submeshes()[item.submeshIndex];
    commands.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
    recordDraw(profiler, submesh.indexCount);
}
