#include "render/GpuScene.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "core/Vertex.h"
#include "render/RhiUtils.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/matrix.hpp>

void GpuScene::init(
    rhi::IDevice&          targetDevice,
    const AssetManager&    targetAssets,
    RenderResourceManager& targetResources,
    ShaderManager&         targetShaders
)
{
    device    = &targetDevice;
    assets    = &targetAssets;
    resources = &targetResources;
    shaders   = &targetShaders;

    materialTable.init(*device, *resources);
    analytic.init(*device);
    emissive.init(*device);
}

SceneUpdateFlags GpuScene::update(
    const Scene& source, uint64_t sceneGeneration, glm::uvec2 extent, rhi::ICommandEncoder& commands
)
{
    auto updates = SceneUpdateFlags::None;
    if (generation != sceneGeneration)
    {
        generation = sceneGeneration;
        materialTable.reset();
        draws.clear();
        objectModels.clear();
        previousObjects.clear();
        meshSpans.clear();
        materialAssetRevision.reset();
        acceleration.invalidate();
        emissive.invalidate();
        geometryBuffersDirty = true;
        instanceBuffersDirty = true;
        updates = SceneUpdateFlags::Scene | SceneUpdateFlags::Geometry | SceneUpdateFlags::Materials;
    }

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
        assets->contentRevision<StandardMaterial>(),
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
    objectModels.clear();
    objectModels.reserve(objects.size());

    size_t drawIndex = 0;
    bool meshSetChanged = meshAssetRevision != meshRevision || objectMeshesChanged;
    bool geometryChanged = false;
    bool materialBindingsChanged = false;
    for (const auto& object : objects)
    {
        const auto& asset = assets->get(object.meshId);
        if (asset.submeshes.empty())
            continue;

        const auto& mesh = resources->loadMesh(object.meshId, commands);
        const auto model = object.transform.matrix();
        const uint32_t matrixID = static_cast<uint32_t>(objectModels.size());
        objectModels.push_back(model);

        for (const auto& submesh : asset.submeshes)
        {
            const bool added = drawIndex >= previousDrawCount;
            auto& draw = added ? draws.emplace_back() : draws[drawIndex];
            const auto materialIndex = resolveMaterials
                ? materialTable.resolve(submesh.materialId, *assets)
                : draw.materialIndex;

            const bool placementChanged = added || draw.mesh != &mesh || draw.firstIndex != submesh.firstIndex ||
                draw.indexCount != submesh.indexCount || draw.model != model || draw.matrixID != matrixID;
            const bool bindingChanged = added || draw.materialIndex != materialIndex;
            if ((placementChanged || bindingChanged) &&
                ((!added && materialTable.info(draw.materialIndex).emissive) || materialTable.info(materialIndex).emissive))
                emissive.invalidate();

            meshSetChanged |= added || draw.mesh != &mesh;
            geometryChanged |= placementChanged;
            materialBindingsChanged |= bindingChanged;
            draw = {
                .mesh          = &mesh,
                .asset         = &asset,
                .model         = model,
                .matrixID      = matrixID,
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
    geometryBuffersDirty |= meshSetChanged || drawIndex != previousDrawCount;
    instanceBuffersDirty |= geometryChanged || materialBindingsChanged;
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
        const float halfHeight = std::tan(glm::radians(camera.fieldOfView) * 0.5f);
        cameraData = {
            .viewMat     = view,
            .viewProjMat = viewProjection,
            .posW        = camera.position,
            .cameraU     = camera.right() * (halfHeight * aspect),
            .cameraV     = camera.up() * halfHeight,
            .cameraW     = camera.forward(),
        };
        previousCamera = camera;
        cameraAspect   = aspect;
    }

    lightCount = static_cast<uint32_t>(analytic.data().size());

    return cameraChanged ? SceneUpdateFlags::Camera : SceneUpdateFlags::None;
}

rhi::IAccelerationStructure* GpuScene::prepareRayTracing(rhi::ICommandEncoder& commands)
{
    auto* tlas = acceleration.getOrBuild(*device, commands, draws);
    if (geometryBuffersDirty)
    {
        uploadGeometryBuffers(commands);
        geometryBuffersDirty = false;
        instanceBuffersDirty = true;
    }
    if (instanceBuffersDirty)
    {
        uploadInstanceData(commands);
        instanceBuffersDirty = false;
    }

    return tlas;
}

void GpuScene::prepareEmissiveLights(rhi::ICommandEncoder& commands)
{
    if (!emissive.isDirty())
        return;

    auto* tlas = prepareRayTracing(commands);
    emissive.prepare(*device, commands, *shaders, *this, tlas, draws, materialTable);
}

void GpuScene::uploadSceneBuffer(
    rhi::ICommandEncoder&      commands,
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

void GpuScene::uploadGeometryBuffers(rhi::ICommandEncoder& commands)
{
    meshSpans.clear();
    std::vector<const GpuMesh*> uniqueMeshes;
    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;

    for (const auto& draw : draws)
    {
        const auto [it, inserted] = meshSpans.try_emplace(draw.mesh);
        if (!inserted)
            continue;

        it->second = {
            .geometryID   = static_cast<uint32_t>(uniqueMeshes.size()),
            .vbOffset     = totalVertices,
            .ibBaseOffset = totalIndices,
        };
        uniqueMeshes.push_back(draw.mesh);
        totalVertices += draw.mesh->vertexCount();
        totalIndices += draw.mesh->indexCount();
    }

    const auto usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;

    rhi::BufferDesc vertexDesc;
    vertexDesc.size  = uint64_t(std::max(totalVertices, 1u)) * sizeof(Vertex);
    vertexDesc.usage = usage;
    rhiCheck(device->createBuffer(vertexDesc, nullptr, vertexBuffer.writeRef()));

    rhi::BufferDesc indexDesc;
    indexDesc.size  = uint64_t(std::max(totalIndices, 1u)) * sizeof(uint32_t);
    indexDesc.usage = usage;
    rhiCheck(device->createBuffer(indexDesc, nullptr, indexBuffer.writeRef()));

    if (uniqueMeshes.empty())
        return;

    for (const GpuMesh* mesh : uniqueMeshes)
    {
        const auto& span = meshSpans.at(mesh);
        if (mesh->vertexCount() > 0)
            commands.copyBuffer(
                vertexBuffer,
                uint64_t(span.vbOffset) * sizeof(Vertex),
                mesh->vertices(),
                0,
                uint64_t(mesh->vertexCount()) * sizeof(Vertex)
            );
        if (mesh->indexCount() > 0)
            commands.copyBuffer(
                indexBuffer,
                uint64_t(span.ibBaseOffset) * sizeof(uint32_t),
                mesh->indices(),
                0,
                uint64_t(mesh->indexCount()) * sizeof(uint32_t)
            );
    }
    commands.globalBarrier();
}

void GpuScene::uploadInstanceData(rhi::ICommandEncoder& commands)
{
    const size_t matrixCount = std::max<size_t>(objectModels.size(), 1);
    std::vector<glm::mat4> worldMatrices(matrixCount, glm::mat4{1.0f});
    std::vector<glm::mat4> inverseTransposeWorldMatrices(matrixCount, glm::mat4{1.0f});
    for (size_t index = 0; index < objectModels.size(); ++index)
    {
        worldMatrices[index] = objectModels[index];
        inverseTransposeWorldMatrices[index] = glm::transpose(glm::inverse(objectModels[index]));
    }

    std::vector<GeometryInstanceData> instances(std::max<size_t>(draws.size(), 1));
    for (uint32_t index = 0; index < draws.size(); ++index)
    {
        const auto& draw = draws[index];
        const auto& span = meshSpans.at(draw.mesh);
        const bool flipped = glm::determinant(glm::mat3(draw.model)) < 0.0f;

        uint32_t flags = static_cast<uint32_t>(GeometryType::TriangleMesh) << GeometryInstanceData::kTypeOffset;
        if (flipped)
            flags |= static_cast<uint32_t>(
                GeometryInstanceFlags::TransformFlipped | GeometryInstanceFlags::IsWorldFrontFaceCW
            );

        instances[index] = {
            .flags          = flags,
            .globalMatrixID = draw.matrixID,
            .materialID     = draw.materialIndex,
            .geometryID     = span.geometryID,
            .vbOffset       = span.vbOffset,
            .ibOffset       = span.ibBaseOffset + draw.firstIndex,
            .instanceIndex  = index,
            .geometryIndex  = 0,
        };
    }

    uploadSceneBuffer(commands, worldMatrixBuffer, std::as_bytes(std::span{worldMatrices}));
    uploadSceneBuffer(
        commands, inverseTransposeWorldMatrixBuffer, std::as_bytes(std::span{inverseTransposeWorldMatrices})
    );
    uploadSceneBuffer(commands, geometryInstanceBuffer, std::as_bytes(std::span{instances}));
}

void GpuScene::bindSceneData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const
{
    const auto scene = rhi::ShaderCursor(&object)["gScene"].getDereferenced();
    rhiCheck(scene["rtAccel"].setBinding(tlas));
    rhiCheck(scene["worldMatrices"].setBinding(worldMatrixBuffer));
    rhiCheck(scene["inverseTransposeWorldMatrices"].setBinding(inverseTransposeWorldMatrixBuffer));
    rhiCheck(scene["geometryInstances"].setBinding(geometryInstanceBuffer));
    rhiCheck(scene["vertices"].setBinding(vertexBuffer));
    rhiCheck(scene["indexData"].setBinding(indexBuffer));
    materialTable.bind(object, scene["materials"]);
    rhiCheck(scene["lightCount"].setData(lightCount));
    rhiCheck(scene["lights"].setBinding(analytic.buffer()));
    emissive.bind(scene["lightCollection"]);
    rhiCheck(scene["camera"]["data"].setData(cameraData));

    const auto envMap = scene["envMap"];
    if (envMap["envMap"].isValid())
    {
        CHECK(environment);
        environment->bind(envMap);
    }
}

void GpuScene::raytrace(
    rhi::ICommandEncoder&     commands,
    rhi::IRayTracingPipeline& pipeline,
    rhi::IShaderTable&        shaderTable,
    rhi::IShaderObject&       shaderObject,
    rhi::Extent3D             extent
)
{
    auto* tlas = prepareRayTracing(commands);
    bindSceneData(shaderObject, tlas);

    auto* pass = commands.beginRayTracingPass();
    CHECK(pass);
    pass->bindPipeline(&pipeline, &shaderTable, &shaderObject);
    pass->dispatchRays(0, extent.width, extent.height, extent.depth);
    pass->end();
}
