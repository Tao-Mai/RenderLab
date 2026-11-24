#include "render/raster/MeshDraw.h"

#include <array>
#include <slang-rhi/shader-cursor.h>
#include "core/Vertex.h"
#include "render/device/RhiCheck.h"
#include "render/resources/GpuMesh.h"

namespace
{
struct MeshPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t  materialIndex = 0;
    uint32_t  padding[3]{};
};
static_assert(sizeof(MeshPushConstants) == 80);
static_assert(offsetof(MeshPushConstants, materialIndex) == 64);
}

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

rhi::IShaderObject& MeshCullPipelines::bind(
    rhi::IRenderPassEncoder& pass, const MaterialTable::MaterialInfo& material
) const
{
    auto* object = pass.bindPipeline(material.doubleSided ? doubleSided.get() : singleSided.get());
    CHECK(object);
    return *object;
}

void MeshDrawRecorder::draw(const GpuDraw& item, rhi::IShaderObject& object)
{
    const rhi::ShaderCursor root(&object);
    scene.bindShaderData(object);
    scene.materials().bind(object);
    const MeshPushConstants constants{.model = item.model, .materialIndex = item.materialIndex};
    rhiCheck(root["pushConstants"].getDereferenced().setData(constants));

    const auto& mesh        = *item.mesh;
    state.vertexBufferCount = 1;
    state.vertexBuffers[0]  = {mesh.vertices(), 0};
    state.indexBuffer       = {mesh.indices(), 0};
    state.indexFormat       = rhi::IndexFormat::Uint32;
    pass.setRenderState(state);
    pass.drawIndexed({item.indexCount, 1, 0, 0, item.firstIndex});
}
