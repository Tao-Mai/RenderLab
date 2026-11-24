#include "render/raster/MeshDraw.h"
#include <array>
#include "core/Vertex.h"
#include "render/device/RhiCheck.h"
#include "render/raster/ShadowResources.h"

rhi::ComPtr<rhi::IInputLayout> createMeshInputLayout(rhi::IDevice& device)
{
    const std::array attributes{
        rhi::InputElementDesc{"POSITION", 0, rhi::Format::RGB32Float, offsetof(Vertex, position)},
        rhi::InputElementDesc{"NORMAL", 0, rhi::Format::RGB32Float, offsetof(Vertex, normal)},
        rhi::InputElementDesc{"TEXCOORD", 0, rhi::Format::RG32Float, offsetof(Vertex, texcoord)},
        rhi::InputElementDesc{"TANGENT", 0, rhi::Format::RGBA32Float, offsetof(Vertex, tangent)}
    };
    const rhi::VertexStreamDesc stream{sizeof(Vertex)};
    rhi::InputLayoutDesc        desc;
    desc.inputElements     = attributes.data();
    desc.inputElementCount = uint32_t(attributes.size());
    desc.vertexStreams     = &stream;
    desc.vertexStreamCount = 1;
    rhi::ComPtr<rhi::IInputLayout> layout;
    rhiCheck(device.createInputLayout(desc, layout.writeRef()));
    return layout;
}
MeshCullPipelines createMeshCullPipelines(rhi::IDevice& device, rhi::RenderPipelineDesc desc)
{
    const auto input              = createMeshInputLayout(device);
    desc.inputLayout              = input;
    desc.rasterizer.frontFace     = rhi::FrontFaceMode::CounterClockwise;
    desc.rasterizer.scissorEnable = true;
    desc.rasterizer.cullMode      = rhi::CullMode::Back;
    MeshCullPipelines pipelines;
    rhiCheck(device.createRenderPipeline(desc, pipelines.singleSided.writeRef()));
    desc.rasterizer.cullMode = rhi::CullMode::None;
    rhiCheck(device.createRenderPipeline(desc, pipelines.doubleSided.writeRef()));
    return pipelines;
}
void MeshDrawRecorder::draw(const DrawItem& item, uint32_t materialIndex, rhi::IRenderPipeline* pipeline)
{
    auto* object = pass.bindPipeline(pipeline);
    CHECK(object);
    const rhi::ShaderCursor root(object);
    rhiCheck(root["sceneGlobals"].setBinding(frame.scene->globalsBuffer()));
    if (shadows)
    {
        frame.scene->lighting.bindRaster(root, ambientCoefficient, frame.scene->environmentMap);
        shadows->bind(root);
    }
    frame.materialTable->bind(object);
    const MeshPushConstants constants{.model = item.model, .materialIndex = materialIndex};
    rhiCheck(root["pushConstants"].getDereferenced().setData(constants));

    const auto& mesh        = frame.resources->mesh(item.mesh);
    state.vertexBufferCount = 1;
    state.vertexBuffers[0]  = {mesh.vertices(), 0};
    state.indexBuffer       = {mesh.indices(), 0};
    state.indexFormat       = rhi::IndexFormat::Uint32;
    pass.setRenderState(state);
    const auto& submesh = mesh.submeshes()[item.submeshIndex];
    pass.drawIndexed({submesh.indexCount, 1, 0, 0, submesh.firstIndex});
}
