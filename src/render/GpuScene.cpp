#include "render/GpuScene.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/Vertex.h"
#include "render/device/BufferCapacity.h"
#include "render/device/RhiCheck.h"
#include "render/resources/RenderResourceManager.h"
#include "scene/system/SceneRenderData.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <glm/matrix.hpp>
#include <limits>

namespace
{
struct alignas(16) RayHitData
{
    uint64_t vertexAddress, indexAddress;
    uint32_t firstIndex, materialIndex, reverseOrientation, firstAreaLight;
};
static_assert(sizeof(RayHitData) == 32);
}  // namespace

void GpuScene::init(
    rhi::IDevice&           targetDevice,
    const AssetManager&     targetAssets,
    const AssetDataManager& targetAssetData,
    RenderResourceManager&  targetResources
)
{
    device    = &targetDevice;
    assets    = &targetAssets;
    assetData = &targetAssetData;
    resources = &targetResources;

    materialTable.init(*device, *resources);
    rhiCheck(device->createBuffer(
        {.size = sizeof(SceneGlobals), .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr,
        globals.writeRef()
    ));
    rhiCheck(device->createBuffer(
        {.size  = kMaxLights * sizeof(LightData),
         .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr,
        lightDataBuffer.writeRef()
    ));

    rhiCheck(resources->texture(BuiltinAssets::Texture::kBrdfLut)->getDefaultView(brdfLutView.writeRef()));
    rhiCheck(resources->texture(BuiltinAssets::Texture::kSheenLut)->getDefaultView(sheenLutView.writeRef()));
    lutSampler = resources->sampler(BuiltinAssets::Sampler::linearClamp);

    uploadedLightRevision.reset();
}

void GpuScene::update(const SceneRenderData& source, rhi::ICommandEncoder& commands)
{
    generation = source.sceneGeneration;

    const auto assetRevision = assets->contentRevision();
    if (preparedAssetRevision != assetRevision || previousItems != source.drawItems || previousLights != source.lights)
    {
        const auto materialIndices = materialTable.prepare(source.drawItems, *assets, *assetData);
        draws.clear();
        draws.reserve(source.drawItems.size());
        for (size_t index = 0; index < source.drawItems.size(); ++index)
        {
            const auto& item = source.drawItems[index];
            const auto& asset = assets->get(item.mesh);
            const auto& submesh = asset.submeshes[item.submeshIndex];
            draws.push_back({
                .mesh                = &resources->loadMesh(item.mesh),
                .asset               = &asset,
                .model               = item.model,
                .bounds              = asset.bounds.transformed(item.model),
                .firstIndex          = submesh.firstIndex,
                .indexCount          = submesh.indexCount,
                .materialIndex       = materialIndices[index],
                .selectionId         = item.selectionId,
                .mirroredPbrtSurface = item.pbrtSurface && glm::determinant(glm::mat3{item.model}) < 0.0f,
                .reverseOrientation = item.pbrtSurface && item.pbrtSurface->reverseOrientation,
            });
        }
        sceneShading.update(source.lights, *assetData, materialTable);

        previousItems         = source.drawItems;
        previousLights        = source.lights;
        preparedAssetRevision = assetRevision;
    }
    environment = source.environmentMap ? &resources->environmentMap(*source.environmentMap) : nullptr;

    viewCulled = false;
    frameTlas  = nullptr;

    sceneViewport = source.viewport;

    const auto& camera         = source.camera;
    const auto  view           = camera.viewMatrix();
    const auto  viewProjection = camera.projectionMatrix(sceneViewport.width / sceneViewport.height) * view;
    sceneGlobals.camera        = {
               .viewProjection        = viewProjection,
               .inverseViewProjection = glm::inverse(viewProjection),
               .cameraPosition        = glm::vec4{camera.position, 1.0f},
               .view                  = view,
    };
    sceneGlobals.lightRanges       = lightRanges();
    sceneGlobals.prefilteredMaxLod = environment ? environment->prefilteredMaxLod : 0.0f;

    rhiCheck(commands.uploadBufferData(globals, 0, sizeof(sceneGlobals), &sceneGlobals));

    const auto revision = shadingRevision();
    if (uploadedLightRevision != revision)
    {
        const auto sceneLights = lights();
        if (!sceneLights.empty())
            rhiCheck(commands.uploadBufferData(lightDataBuffer, 0, sceneLights.size_bytes(), sceneLights.data()));
        uploadedLightRevision = revision;
    }

    materialTable.upload(*device, commands);
}

std::span<const uint32_t> GpuScene::visibleDraws()
{
    if (!viewCulled)
    {
        const Frustum frustum{cameraUniforms().viewProjection};
        visibleDrawIndices.clear();
        visibleDrawIndices.reserve(draws.size());
        for (uint32_t index = 0; index < draws.size(); ++index)
            if (frustum.intersects(draws[index].bounds))
                visibleDrawIndices.push_back(index);
        viewCulled = true;
    }
    return visibleDrawIndices;
}

GpuScene::RayTracingData GpuScene::prepareRayTracing(rhi::ICommandEncoder& commands, uint32_t frameIndex)
{
    if (frameTlas)
        return {*this, frameTlas};

    const auto revision = shadingRevision();
    if (uploadedRayRevision != revision)
    {
        sceneShading.buildEmissiveTriangles(draws, *assetData, materialTable);
        uploadRayTracingData(commands);
        uploadedRayRevision = revision;
    }

    const auto& ranges     = lightRanges();
    const auto  rectLights = lights().subspan(ranges.pointEnd, ranges.rectAreaEnd - ranges.pointEnd);
    frameTlas = acceleration.recordBuilds(*device, commands, draws, rectLights, ranges.pointEnd, frameIndex);
    return {*this, frameTlas};
}

void GpuScene::uploadRayTracingData(rhi::ICommandEncoder& commands)
{
    const auto upload =
        [&](rhi::ComPtr<rhi::IBuffer>& buffer, std::span<const std::byte> bytes, rhi::BufferDesc desc = {})
    {
        if (buffer)
            ensureBufferCapacity(*device, buffer, bytes.size());
        else
        {
            desc.size = std::max<uint64_t>(desc.size, bytes.size());
            desc.usage |= rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
            rhiCheck(device->createBuffer(desc, nullptr, buffer.writeRef()));
        }
        rhiCheck(commands.uploadBufferData(buffer, 0, bytes.size(), bytes.data()));
    };

    static_assert(
        sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24 &&
        offsetof(Vertex, tangent) == 32
    );
    const auto&             shadingData = sceneShading.data();
    std::vector<RayHitData> hits(std::max<size_t>(draws.size(), 1));
    for (uint32_t index = 0; index < draws.size(); ++index)
    {
        const auto& draw = draws[index];
        const auto& mesh = *draw.mesh;

        hits[index] = {
            .vertexAddress      = mesh.vertices()->getDeviceAddress(),
            .indexAddress       = mesh.indices()->getDeviceAddress(),
            .firstIndex         = draw.firstIndex,
            .materialIndex      = draw.materialIndex,
            .reverseOrientation = draw.reverseOrientation ? 1u : 0u,
            .firstAreaLight     = draw.firstAreaLight,
        };
    }
    upload(rayHitBuffer, std::as_bytes(std::span{hits}));

    const std::array<uint32_t, 4> triangleHeader{
        static_cast<uint32_t>(shadingData.lightTriangles.size()),
        shadingData.firstRectTriangle,
        shadingData.lightRanges.pointEnd
    };
    static_assert(sizeof(triangleHeader) == kLightTriangleHeaderSize);
    const auto triangleBytes = std::as_bytes(std::span{shadingData.lightTriangles});
    CHECK(
        triangleBytes.size() <= std::numeric_limits<uint32_t>::max() - sizeof(triangleHeader),
        "triangle light table exceeds the shader buffer address range"
    );
    std::vector<std::byte> triangleData(sizeof(triangleHeader) + triangleBytes.size());
    std::memcpy(triangleData.data(), triangleHeader.data(), sizeof(triangleHeader));
    if (!triangleBytes.empty())
        std::memcpy(triangleData.data() + sizeof(triangleHeader), triangleBytes.data(), triangleBytes.size());
    upload(
        triangleLights,
        triangleData,
        {.size = kLightTriangleHeaderSize + kLightTriangleStride, .usage = rhi::BufferUsage::CopySource}
    );

    const std::array<std::byte, sizeof(GpuLightEmission)> emptyEmission{};
    const auto emissions = shadingData.areaEmissions.empty() ? std::span<const std::byte>{emptyEmission}
                                                             : std::as_bytes(std::span{shadingData.areaEmissions});
    upload(areaLightEmissions, emissions);
    upload(environmentLightBuffer, std::as_bytes(std::span{&shadingData.environmentLight, 1}));
}

void GpuScene::RayTracingData::bind(rhi::IShaderObject* object) const
{
    scene.materialTable.bind(*scene.device, *object);
    const rhi::ShaderCursor root(object);
    rhiCheck(root["sceneGlobals"].setBinding(scene.globals));
    rhiCheck(root["scene"].setBinding(tlas));
    rhiCheck(root["lights"].setBinding(scene.lightDataBuffer));
    rhiCheck(root["objects"].setBinding(scene.rayHitBuffer));
    rhiCheck(root["lightTriangles"].setBinding(scene.triangleLights));
    rhiCheck(root["areaEmissions"].setBinding(scene.areaLightEmissions));
    if (const auto field = root["environmentLight"]; field.isValid())
        rhiCheck(field.setBinding(scene.environmentLightBuffer));
    if (scene.environment)
        scene.environment->bindRadiance(root);
}
