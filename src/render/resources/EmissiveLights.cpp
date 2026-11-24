#include "render/resources/EmissiveLights.h"

#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/GpuScene.h"
#include "render/device/BufferCapacity.h"
#include "render/resources/GpuMesh.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/SceneLightConstants.h"
#include "render/resources/ShaderManager.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
struct alignas(16) EmissiveMesh
{
    uint64_t vertexAddress, indexAddress;
    glm::mat4 model;
    uint32_t firstIndex, materialIndex, triangleOffset, flipNormal;
    float emissionWeight;
    uint32_t padding[3]{};
};
static_assert(sizeof(EmissiveMesh) == 112);
static_assert(offsetof(EmissiveMesh, model) == 16);
static_assert(offsetof(EmissiveMesh, emissionWeight) == 96);
}  // namespace

void EmissiveLights::prepare(
    rhi::IDevice& device, rhi::ICommandEncoder& commands, ShaderManager& shaders,
    std::span<const GpuDraw> draws, const MaterialTable& materials
)
{
    if (!dirty)
        return;

    std::vector<EmissiveMesh> meshes;
    std::vector<uint32_t> offsets(std::max<size_t>(draws.size(), 1), kInvalidLightIndex);
    triangleCount = 0;
    for (size_t index = 0; index < draws.size(); ++index)
    {
        const auto& draw = draws[index];
        if (!materials.info(draw.materialIndex).emissive || draw.indexCount == 0)
            continue;

        const auto count = draw.indexCount / 3;
        CHECK(draw.indexCount % 3 == 0 && uint64_t(triangleCount) + count <=
            std::numeric_limits<uint32_t>::max() / kEmissiveTriangleStride, "invalid emissive triangle count");
        const auto& material = materials.data(draw.materialIndex);
        const auto color = material.emissiveFactor;
        const float sides = (material.emissiveFlags & kEmissiveTwoSided) ? 2.0f : 1.0f;
        offsets[index] = triangleCount;
        meshes.push_back({
            .vertexAddress = draw.mesh->vertices()->getDeviceAddress(),
            .indexAddress = draw.mesh->indices()->getDeviceAddress(),
            .model = draw.model,
            .firstIndex = draw.firstIndex,
            .materialIndex = draw.materialIndex,
            .triangleOffset = triangleCount,
            .flipNormal = uint32_t(draw.mirroredPbrtSurface != draw.reverseOrientation),
            .emissionWeight = sides * std::max({color.r, color.g, color.b}),
        });
        triangleCount += count;
    }

    const auto allocate = [&](rhi::ComPtr<rhi::IBuffer>& buffer, uint64_t size, rhi::BufferUsage usage)
    {
        if (buffer)
            ensureBufferCapacity(device, buffer, size);
        else
            rhiCheck(device.createBuffer({.size = size, .usage = usage}, nullptr, buffer.writeRef()));
    };
    const auto readUsage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
    allocate(instanceOffsets, offsets.size() * sizeof(uint32_t), readUsage);
    rhiCheck(commands.uploadBufferData(instanceOffsets, 0, offsets.size() * sizeof(uint32_t), offsets.data()));
    allocate(triangles, uint64_t(std::max(triangleCount, 1u)) * kEmissiveTriangleStride,
        rhi::BufferUsage::ShaderResource | rhi::BufferUsage::UnorderedAccess);
    dirty = false;
    if (triangleCount == 0)
        return;

    allocate(meshBuffer, meshes.size() * sizeof(EmissiveMesh), readUsage);
    rhiCheck(commands.uploadBufferData(meshBuffer, 0, meshes.size() * sizeof(EmissiveMesh), meshes.data()));
    if (!buildPipeline)
        rhiCheck(device.createComputePipeline(
            {.program = shaders.getProgram("shaders/scene/build_emissive_lights.slang", {"main"})},
            buildPipeline.writeRef()
        ));

    const struct
    {
        uint32_t meshCount, triangleCount;
        uint32_t padding[2]{};
    } parameters{static_cast<uint32_t>(meshes.size()), triangleCount};
    const auto groups = dispatchGroups(buildPipeline,
        {std::min(triangleCount, kLightDispatchRowWidth),
         (triangleCount + kLightDispatchRowWidth - 1) / kLightDispatchRowWidth, 1});
    // The shader reads vertex and index buffers through device addresses.
    commands.globalBarrier();
    auto* pass = commands.beginComputePass();
    CHECK(pass);
    const rhi::ShaderCursor root(pass->bindPipeline(buildPipeline));
    rhiCheck(root["meshes"].setBinding(meshBuffer));
    rhiCheck(root["triangles"].setBinding(triangles));
    rhiCheck(root["parameters"].getDereferenced().setData(parameters));
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
    commands.globalBarrier();
}

void EmissiveLights::bind(const rhi::ShaderCursor& cursor) const
{
    const auto block = cursor.getDereferenced();
    rhiCheck(block["triangleCount"].setData(triangleCount));
    rhiCheck(block["instanceOffsets"].setBinding(instanceOffsets));
    rhiCheck(block["triangles"].setBinding(triangles));
}
