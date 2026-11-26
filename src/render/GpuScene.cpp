#include "render/GpuScene.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "core/Vertex.h"
#include "render/RhiUtils.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cstddef>
#include <glm/matrix.hpp>

namespace
{
struct alignas(16) RayHitData
{
    uint64_t vertexAddress, indexAddress;
    uint32_t firstIndex, materialIndex;
    uint32_t padding[2]{};
    glm::mat4 objectToWorld, worldToObject;
};
static_assert(offsetof(RayHitData, objectToWorld) == 32 && offsetof(RayHitData, worldToObject) == 96);
static_assert(sizeof(RayHitData) == 160);
}  // namespace

void GpuScene::init(
    rhi::IDevice&           targetDevice,
    const AssetManager&     targetAssets,
    RenderResourceManager&  targetResources,
    ShaderManager&          targetShaders
)
{
    device    = &targetDevice;
    assets    = &targetAssets;
    resources = &targetResources;
    shaders   = &targetShaders;

    materialTable.init(*device, *resources);
    analytic.init(*device);
}

SceneUpdateFlags GpuScene::update(
    const Scene& source, uint64_t sceneGeneration, glm::uvec2 extent, rhi::ICommandEncoder& commands
)
{
    auto updates = generation != sceneGeneration ? SceneUpdateFlags::Scene : SceneUpdateFlags::None;
    generation = sceneGeneration;

    updates |= updateObjects(source.objects, commands);
    
    if (analytic.update(source.analyticLights, *device, commands))
        updates |= SceneUpdateFlags::AnalyticLights;
    
    if (updateEnvironment(source, commands))
        updates |= SceneUpdateFlags::Environment;
    
    if (materialTable.upload(commands))
        updates |= SceneUpdateFlags::Materials;

    return updates | updateGlobals(source.camera, extent);
}

SceneUpdateFlags GpuScene::updateObjects(std::span<const Object> objects, rhi::ICommandEncoder& commands)
{
    const auto meshRevision = assets->contentRevision<MeshAsset>();
    const auto materialRevision = std::max({
        assets->contentRevision<GltfMaterial>(),
        assets->contentRevision<TextureAsset>(),
        assets->contentRevision<SamplerAsset>(),
    });
    const bool objectMeshesChanged = !std::ranges::equal(objects, previousObjects, {}, &Object::meshId, &Object::meshId);
    const bool objectInstancesChanged = objectMeshesChanged ||
        !std::ranges::equal(objects, previousObjects, {}, &Object::transform, &Object::transform);
    const bool resolveMaterials = meshAssetRevision != meshRevision || materialAssetRevision != materialRevision ||
        objectMeshesChanged;
    if (!objectInstancesChanged && !resolveMaterials)
        return SceneUpdateFlags::None;

    const auto previousEmissionRevision = materialTable.emissionRevision();
    const auto previousDrawCount = draws.size();
    size_t drawIndex = 0;
    bool geometryChanged = false;
    bool materialBindingsChanged = false;
    for (const auto& object : objects)
    {
        const auto& asset = assets->get(object.meshId);
        if (asset.submeshes.empty())
            continue;

        const auto& mesh = resources->loadMesh(object.meshId, commands);
        const auto model = object.transform.matrix();
        for (const auto& submesh : asset.submeshes)
        {
            const bool added = drawIndex >= previousDrawCount;
            auto& draw = added ? draws.emplace_back() : draws[drawIndex];
            const auto materialIndex = resolveMaterials
                ? materialTable.resolve(submesh.materialId, *assets)
                : draw.materialIndex;

            const bool placementChanged = added || draw.mesh != &mesh || draw.firstIndex != submesh.firstIndex ||
                draw.indexCount != submesh.indexCount || draw.model != model;
            const bool bindingChanged = added || draw.materialIndex != materialIndex;
            if ((placementChanged || bindingChanged) &&
                ((!added && materialTable.info(draw.materialIndex).emissive) || materialTable.info(materialIndex).emissive))
                emissive.invalidate();

            geometryChanged |= placementChanged;
            materialBindingsChanged |= bindingChanged;
            draw = {
                .mesh          = &mesh,
                .asset         = &asset,
                .model         = model,
                .firstIndex    = submesh.firstIndex,
                .indexCount    = submesh.indexCount,
                .materialIndex = materialIndex,
            };
            ++drawIndex;
        }
    }

    if (drawIndex < previousDrawCount)
        draws.resize(drawIndex);
    if (drawIndex != previousDrawCount || materialTable.emissionRevision() != previousEmissionRevision)
        emissive.invalidate();

    geometryChanged |= drawIndex != previousDrawCount;
    rayHitsDirty |= geometryChanged || materialBindingsChanged;
    if (geometryChanged)
        acceleration.invalidate();

    if (objectInstancesChanged)
        previousObjects.assign(objects.begin(), objects.end());
    meshAssetRevision = meshRevision;
    materialAssetRevision = materialRevision;

    auto updates = SceneUpdateFlags::None;
    if (geometryChanged)
        updates |= SceneUpdateFlags::Geometry;
    if (materialBindingsChanged)
        updates |= SceneUpdateFlags::Materials;

    return updates;
}

bool GpuScene::updateEnvironment(const Scene& source, rhi::ICommandEncoder& commands)
{
    if (environmentMapId == source.environmentMap)
        return false;

    environmentMapId = source.environmentMap;
    environment = environmentMapId ? &resources->environmentMap(*environmentMapId, commands) : nullptr;
    return true;
}

SceneUpdateFlags GpuScene::updateGlobals(const Camera& camera, glm::uvec2 extent)
{
    const auto aspect = float(extent.x) / float(extent.y);
    const bool cameraChanged = previousCamera != camera || cameraAspect != aspect;
    if (cameraChanged)
    {
        const auto view = camera.viewMatrix();
        const auto viewProjection = camera.projectionMatrix(aspect) * view;
        sceneGlobals.camera = {
            .viewProjection        = viewProjection,
            .inverseViewProjection = glm::inverse(viewProjection),
            .cameraPosition        = glm::vec4{camera.position, 1.0f},
            .view                  = view,
        };
        previousCamera = camera;
        cameraAspect   = aspect;
    }

    sceneGlobals.analyticLightCount = static_cast<uint32_t>(analytic.data().size());
    return cameraChanged ? SceneUpdateFlags::Camera : SceneUpdateFlags::None;
}

rhi::IAccelerationStructure* GpuScene::prepareRayTracing(rhi::ICommandEncoder& commands)
{
    auto* tlas = acceleration.getOrBuild(*device, commands, draws);
    if (rayHitsDirty)
    {
        uploadRayHits(commands);
        rayHitsDirty = false;
    }
    return tlas;
}

void GpuScene::prepareEmissiveLights(rhi::ICommandEncoder& commands)
{
    emissive.prepare(*device, commands, *shaders, draws, materialTable);
}

void GpuScene::uploadSceneBuffer(
    rhi::ICommandEncoder& commands,
    rhi::ComPtr<rhi::IBuffer>& buffer,
    std::span<const std::byte> bytes
)
{
    if (buffer)
        ensureBufferCapacity(*device, buffer, bytes.size());
    else
    {
        rhi::BufferDesc desc;
        desc.size = bytes.size();
        desc.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
        rhiCheck(device->createBuffer(desc, nullptr, buffer.writeRef()));
    }
    rhiCheck(commands.uploadBufferData(buffer, 0, bytes.size(), bytes.data()));
}

void GpuScene::uploadRayHits(rhi::ICommandEncoder& commands)
{
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
            .objectToWorld      = draw.model,
            .worldToObject      = glm::inverse(draw.model),
        };
    }
    
    uploadSceneBuffer(commands, rayHitBuffer, std::as_bytes(std::span{hits}));
}

void GpuScene::bindSceneData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const
{
    const auto scene = rhi::ShaderCursor(&object)["scene"].getDereferenced();
    rhiCheck(scene.setData(sceneGlobals));
    rhiCheck(scene["analyticLights"].setBinding(analytic.buffer()));
    rhiCheck(scene["rtAccel"].setBinding(tlas));
    rhiCheck(scene["objects"].setBinding(rayHitBuffer));
}

void GpuScene::bindRayTracingGeometryData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const
{
    bindSceneData(object, tlas);
    materialTable.bind(object);
}

void GpuScene::bindRayTracingShaderData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const
{
    bindRayTracingGeometryData(object, tlas);
    const rhi::ShaderCursor root(&object);
    emissive.bind(root["emissiveLights"]);
    if (environment)
        environment->bindRadiance(root);
}

void GpuScene::raytrace(
    rhi::ICommandEncoder& commands,
    rhi::IRayTracingPipeline& pipeline,
    rhi::IShaderTable& shaderTable,
    rhi::IShaderObject& shaderObject,
    rhi::Extent3D extent
)
{
    auto* tlas = prepareRayTracing(commands);
    bindRayTracingShaderData(shaderObject, tlas);

    auto* pass = commands.beginRayTracingPass();
    CHECK(pass);
    pass->bindPipeline(&pipeline, &shaderTable, &shaderObject);
    pass->dispatchRays(0, extent.width, extent.height, extent.depth);
    pass->end();
}
