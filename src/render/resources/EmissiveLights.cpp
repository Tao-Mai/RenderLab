#include "render/resources/EmissiveLights.h"

#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
#include "render/RhiUtils.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/SceneLightConstants.h"
#include "render/resources/ShaderManager.h"

#include <algorithm>
#include <vector>

namespace
{
struct BuildParameters
{
    uint32_t materialID;
    uint32_t instanceID;
    uint32_t triangleCount;
    uint32_t triangleOffset;
};
static_assert(sizeof(BuildParameters) == 16);
}  // namespace

void EmissiveLights::init(rhi::IDevice& device)
{
    rhiCheck(device.createBuffer(
        {.size = sizeof(uint32_t),
         .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr,
        instanceOffsets.writeRef()
    ));
    rhiCheck(device.createBuffer(
        {.size = kEmissiveTriangleStride,
         .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess},
        nullptr,
        triangles.writeRef()
    ));
}

void EmissiveLights::prepare(
    rhi::IDevice&                device,
    rhi::ICommandEncoder&        commands,
    ShaderManager&               shaders,
    const GpuScene&              scene,
    rhi::IAccelerationStructure* tlas,
    std::span<const GpuDraw>     draws,
    const MaterialTable&         materials
)
{
    if (!dirty)
        return;

    std::vector<BuildParameters> emissiveMeshes;
    std::vector<uint32_t>        offsets(std::max<size_t>(draws.size(), 1), kInvalidLightIndex);
    triangleCount = 0;
    for (uint32_t index = 0; index < draws.size(); ++index)
    {
        const auto& draw = draws[index];
        if (!materials.info(draw.materialIndex).emissive || draw.indexCount == 0)
            continue;

        const uint32_t count = draw.indexCount / 3;
        offsets[index] = triangleCount;
        emissiveMeshes.push_back({
            .materialID     = draw.materialIndex,
            .instanceID     = index,
            .triangleCount  = count,
            .triangleOffset = triangleCount,
        });
        triangleCount += count;
    }

    const auto readUsage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
    if (instanceOffsets)
        ensureBufferCapacity(device, instanceOffsets, offsets.size() * sizeof(uint32_t));
    else
        rhiCheck(device.createBuffer(
            {.size = offsets.size() * sizeof(uint32_t), .usage = readUsage}, nullptr, instanceOffsets.writeRef()
        ));
    rhiCheck(commands.uploadBufferData(instanceOffsets, 0, offsets.size() * sizeof(uint32_t), offsets.data()));

    dirty = false;
    if (triangleCount == 0)
        return;

    // Allocate a new buffer so binding gScene.lightCollection.triangleData (SRV) never aliases gTriangleData (UAV).
    rhi::ComPtr<rhi::IBuffer> builtTriangles;
    rhiCheck(device.createBuffer(
        {.size  = uint64_t(triangleCount) * kEmissiveTriangleStride,
         .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess},
        nullptr,
        builtTriangles.writeRef()
    ));

    if (!buildPipeline)
        rhiCheck(device.createComputePipeline(
            {.program = shaders.getProgram("Scene/Lights/BuildTriangleList.cs.slang", {"buildTriangleList"})},
            buildPipeline.writeRef()
        ));

    auto* pass = commands.beginComputePass();
    CHECK(pass);
    for (const auto& mesh : emissiveMeshes)
    {
        auto* rootObject = pass->bindPipeline(buildPipeline);
        CHECK(rootObject);
        scene.bindSceneData(*rootObject, tlas);

        const rhi::ShaderCursor root(rootObject);
        rhiCheck(root["gTriangleData"].setBinding(builtTriangles));
        rhiCheck(root["CB"].getDereferenced().setData(mesh));

        const auto groups = dispatchGroups(buildPipeline, {mesh.triangleCount, 1, 1});
        pass->dispatchCompute(groups.width, groups.height, groups.depth);
    }
    pass->end();
    commands.globalBarrier();

    triangles = std::move(builtTriangles);
}

void EmissiveLights::bind(const rhi::ShaderCursor& cursor) const
{
    rhiCheck(cursor["triangleCount"].setData(triangleCount));
    rhiCheck(cursor["perMeshInstanceOffset"].setBinding(instanceOffsets));
    rhiCheck(cursor["triangleData"].setBinding(triangles));
}
