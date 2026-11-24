#include "render/raytracing/RayScene.h"

#include <slang-rhi/shader-cursor.h>
#include "core/Vertex.h"
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>

namespace
{
struct alignas(16) RayHitData
{
    uint64_t vertexAddress, indexAddress;
    uint32_t firstIndex, materialIndex, reverseOrientation, firstAreaLight;
};
static_assert(sizeof(RayHitData) == 32);
}  // namespace

void RayScene::setup(GpuResources& resources, rhi::IDevice& device)
{
    const auto createBuffer = [&](size_t size, rhi::BufferUsage usage = rhi::BufferUsage::None)
    {
        rhi::ComPtr<rhi::IBuffer> buffer;
        rhiCheck(device.createBuffer(
            {.size = size, .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination | usage},
            nullptr,
            buffer.writeRef()
        ));
        return resources.registerBuffer(buffer);
    };

    cache            = &resources;
    objects          = createBuffer(sizeof(RayHitData));
    triangles        = createBuffer(kLightTriangleHeaderSize + kLightTriangleStride, rhi::BufferUsage::CopySource);
    areaEmissions    = createBuffer(sizeof(GpuLightEmission));
    environmentLight = createBuffer(sizeof(GpuEnvironmentLight));
    uploadedRevision.reset();
}

void RayScene::upload(const PreparedScene& scene, RenderResourceManager& resources)
{
    const auto& shading = *scene.shading;
    if (uploadedRevision == shading.revision)
        return;

    static_assert(
        sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24 &&
        offsetof(Vertex, tangent) == 32
    );
    const auto items        = scene.drawItems;
    const auto objectUpload = cache->allocateUpload(objects, std::max<size_t>(items.size(), 1) * sizeof(RayHitData));
    auto*      hits         = reinterpret_cast<RayHitData*>(objectUpload.data.data());
    for (uint32_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        const auto& mesh = resources.mesh(item.mesh);
        const auto& draw = shading.draws[index];
        const auto& part = mesh.submeshes()[item.submeshIndex];
        std::construct_at(
            hits + index,
            RayHitData{
                .vertexAddress      = mesh.vertices()->getDeviceAddress(),
                .indexAddress       = mesh.indices()->getDeviceAddress(),
                .firstIndex         = part.firstIndex,
                .materialIndex      = draw.materialIndex,
                .reverseOrientation = draw.reverseOrientation,
                .firstAreaLight     = draw.firstAreaLight,
            }
        );
    }

    const std::array<uint32_t, 4> triangleHeader{
        static_cast<uint32_t>(shading.lightTriangles.size()),
        shading.firstRectTriangle,
        shading.lightRanges.pointEnd
    };
    static_assert(sizeof(triangleHeader) == kLightTriangleHeaderSize);
    const auto triangleBytes = shading.lightTriangles.size() * kLightTriangleStride;
    CHECK(
        triangleBytes <= std::numeric_limits<uint32_t>::max() - sizeof(triangleHeader),
        "triangle light table exceeds the shader buffer address range"
    );
    const auto triangleUpload = cache->allocateUpload(triangles, sizeof(triangleHeader) + triangleBytes);
    std::memcpy(triangleUpload.data.data(), triangleHeader.data(), sizeof(triangleHeader));
    if (triangleBytes != 0)
        std::memcpy(triangleUpload.data.data() + sizeof(triangleHeader), shading.lightTriangles.data(), triangleBytes);

    const auto emissionBytes  = shading.areaEmissions.size() * sizeof(GpuLightEmission);
    const auto emissionUpload = cache->allocateUpload(areaEmissions, std::max(emissionBytes, sizeof(GpuLightEmission)));
    if (emissionBytes != 0)
        std::memcpy(emissionUpload.data.data(), shading.areaEmissions.data(), emissionBytes);

    const auto environmentUpload = cache->allocateUpload(environmentLight, sizeof(shading.environmentLight));
    std::memcpy(environmentUpload.data.data(), &shading.environmentLight, sizeof(shading.environmentLight));

    uploadedRevision = shading.revision;
}

void RayScene::bind(rhi::IShaderObject* object, const RenderContext& frame) const
{
    frame.materialTable->bind(object);
    const rhi::ShaderCursor root(object);
    rhiCheck(root["sceneGlobals"].setBinding(frame.scene->globalsBuffer()));
    rhiCheck(root["scene"].setBinding(frame.scene->tlas));
    rhiCheck(root["objects"].setBinding(cache->getBuffer(objects)));
    rhiCheck(root["lights"].setBinding(frame.scene->lighting.getLightBuffer()));
    rhiCheck(root["lightTriangles"].setBinding(getTriangleBuffer()));
    rhiCheck(root["areaEmissions"].setBinding(cache->getBuffer(areaEmissions)));
    if (const auto field = root["environmentLight"]; field.isValid())
        rhiCheck(field.setBinding(cache->getBuffer(environmentLight)));
    if (const auto* environment = frame.scene->environmentMap)
        environment->bindRadiance(root);
}
