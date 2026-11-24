#include "render/raytracing/RealtimeScene.h"

#include <slang-rhi.h>
#include "core/Vertex.h"
#include "render/FrameData.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/raytracing/RealtimeSceneData.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"

namespace
{
struct PrepareParameters
{
    uint32_t triangleCount;
    uint32_t taskCount;
};
static_assert(sizeof(PrepareParameters) == 8);
}  // namespace

void RealtimeScene::compile(const Falcor::RenderContext& context, GpuResources::BufferHandle triangleBuffer)
{
    auto&           cache = *context.gpuResources;
    rhi::BufferDesc desc;
    desc.usage =
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopyDestination;
    rhi::ComPtr<rhi::IBuffer> buffer;
    desc.size = sizeof(RayTracingHitData);
    rhiCheck(context.device->createBuffer(desc, nullptr, buffer.writeRef()));
    objects   = cache.registerBuffer(buffer);
    desc.size = sizeof(EmissiveTriangleTask);
    rhiCheck(context.device->createBuffer(desc, nullptr, buffer.writeRef()));
    tasks = cache.registerBuffer(buffer);
    if (triangleBuffer.id == GpuResources::kInvalidHandle)
    {
        desc.size = sizeof(EmissiveTriangleHeader) + sizeof(EmissiveTriangle);
        rhiCheck(context.device->createBuffer(desc, nullptr, buffer.writeRef()));
        triangleBuffer = cache.registerBuffer(buffer);
    }
    triangles = triangleBuffer;

    triangleCount = 0;
    taskCount     = 0;
    if (!pipeline)
        createPipeline(*context.device, *context.shaderManager);
}


void RealtimeScene::createPipeline(rhi::IDevice& device, ShaderManager& shaders)
{
    rhiCheck(device.createComputePipeline(
        {.program = shaders.program(shaders.getOrLoad("prepare_emissive_triangles"))},
        pipeline.writeRef()
    ));
}


rhi::IBuffer* RealtimeScene::prepare(GpuResources& cache, const Falcor::RenderContext& frame)
{
    const auto& items     = frame.scene->drawItems;
    auto&       resources = *frame.resources;
    const auto& materials = *frame.materialTable;
    static_assert(sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24);

    const auto upload =
        cache.allocateUpload(objects, std::max<size_t>(items.size(), 1) * sizeof(RayTracingHitData));
    auto*                             hits = reinterpret_cast<RayTracingHitData*>(upload.data.data());
    std::vector<EmissiveTriangleTask> lightTasks;
    triangleCount = 0;
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        const auto&       item          = items[index];
        const auto&       mesh          = resources.mesh(item.mesh);
        const auto&       submesh       = mesh.submeshes()[item.submeshIndex];
        const auto        materialIndex = frame.scene->materialIndices[index];
        RayTracingHitData hit{
            .vertexAddress = mesh.vertices()->getDeviceAddress(),
            .indexAddress  = mesh.indices()->getDeviceAddress(),
            .firstIndex    = submesh.firstIndex,
            .materialIndex = materialIndex,
        };
        if (materials.info(materialIndex).emissive && submesh.indexCount > 0)
        {
            const auto count = submesh.indexCount / 3;
            CHECK(
                count < std::numeric_limits<uint32_t>::max() - triangleCount,
                "emissive triangle count exceeds the shader index range"
            );
            hit.firstEmissiveTriangle = triangleCount;
            lightTasks.push_back({item.model, index, triangleCount, count});
            triangleCount += count;
        }
        std::construct_at(hits + index, hit);
    }
    if (items.empty())
    {
        std::construct_at(hits, RayTracingHitData{});
    }

    taskCount = static_cast<uint32_t>(lightTasks.size());
    const auto taskStorageUpload =
        cache.allocateUpload(tasks, std::max<size_t>(taskCount, 1) * sizeof(EmissiveTriangleTask));
    if (taskCount > 0)
    {
        std::memcpy(taskStorageUpload.data.data(), lightTasks.data(), lightTasks.size() * sizeof(EmissiveTriangleTask));
    }
    else
    {
        std::memset(taskStorageUpload.data.data(), 0, sizeof(EmissiveTriangleTask));
    }
    cache.ensureBufferCapacity(
        triangles,
        sizeof(EmissiveTriangleHeader) + uint64_t(triangleCount) * sizeof(EmissiveTriangle)
    );
    const EmissiveTriangleHeader header{triangleCount};
    const auto                   headerUpload = cache.allocateUpload(triangles, sizeof(header));
    std::memcpy(headerUpload.data.data(), &header, sizeof(header));
    updateBindings(cache, frame);
    return cache.getBuffer(triangles);
}


void RealtimeScene::updateBindings(GpuResources& cache, const Falcor::RenderContext& execution)
{
    auto& object = frames[execution.frameIndex].shaderObject;
    rhiCheck(execution.device->createRootShaderObject(pipeline->getProgram(), object.writeRef()));
    const rhi::ShaderCursor root(object);
    rhiCheck(root["objects"].setBinding(cache.getBuffer(objects)));
    rhiCheck(root["tasks"].setBinding(cache.getBuffer(tasks)));
    rhiCheck(root["triangles"].setBinding(cache.getBuffer(triangles)));
}


void RealtimeScene::execute(rhi::ICommandEncoder& commands, const Falcor::RenderContext& frame)
{
    if (triangleCount == 0)
        return;
    const auto&             object = frames[frame.frameIndex].shaderObject;
    const PrepareParameters constants{triangleCount, taskCount};
    rhiCheck(rhi::ShaderCursor(object)["parameters"].getDereferenced().setData(constants));
    auto* pass = commands.beginComputePass();
    CHECK(pass);
    pass->bindPipeline(pipeline, object);
    const auto groups = dispatchGroups(pipeline, {triangleCount, 1, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}

void RealtimeScene::bind(rhi::IShaderObject* object, GpuResources& cache, const Falcor::RenderContext& frame) const
{
    bindFrameData(object, *frame.frameData);
    frame.materialTable->bind(object);
    const rhi::ShaderCursor root(object);
    rhiCheck(root["scene"].setBinding(frame.sceneTlas));
    rhiCheck(root["objects"].setBinding(cache.getBuffer(objects)));
    rhiCheck(root["emissiveTriangles"].setBinding(cache.getBuffer(triangles)));
}
