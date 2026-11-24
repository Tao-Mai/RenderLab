#include "render/GpuScene.h"

#include <slang-rhi/shader-cursor.h>
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/pbrt/PbrtSpectrum.h"
#include "core/Vertex.h"
#include "render/device/BufferCapacity.h"
#include "render/device/RhiCheck.h"
#include "render/resources/RenderResourceManager.h"
#include "scene/system/SceneRenderData.h"

#include <algorithm>
#include <cstddef>
#include <glm/matrix.hpp>

namespace
{
struct alignas(16) RayHitData
{
    uint64_t vertexAddress, indexAddress;
    uint32_t firstIndex, materialIndex, reverseOrientation, padding;
    glm::mat4 objectToWorld, worldToObject;
};
static_assert(offsetof(RayHitData, objectToWorld) == 32 && offsetof(RayHitData, worldToObject) == 96);
static_assert(sizeof(RayHitData) == 160);
}  // namespace

void GpuScene::init(
    rhi::IDevice&           targetDevice,
    const AssetManager&     targetAssets,
    const AssetDataManager& targetAssetData,
    RenderResourceManager&  targetResources,
    ShaderManager&          targetShaders
)
{
    device    = &targetDevice;
    assets    = &targetAssets;
    assetData = &targetAssetData;
    resources = &targetResources;
    shaders   = &targetShaders;

    materialTable.init(*device, *resources);
    analytic.init(*device);
    rhiCheck(device->createBuffer(
        {.size = sizeof(SceneGlobals), .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr,
        globals.writeRef()
    ));
    lutSampler = resources->sampler(BuiltinAssets::Sampler::linearClamp);
}

SceneUpdateFlags GpuScene::update(const SceneRenderData& source, rhi::ICommandEncoder& commands)
{
    auto updates = generation != source.sceneGeneration ? SceneUpdateFlags::Scene : SceneUpdateFlags::None;
    generation = source.sceneGeneration;

    if (!brdfLutView)
    {
        rhiCheck(resources->texture(BuiltinAssets::Texture::kBrdfLut, commands)->getDefaultView(brdfLutView.writeRef()));
        rhiCheck(resources->texture(BuiltinAssets::Texture::kSheenLut, commands)->getDefaultView(sheenLutView.writeRef()));
    }

    const auto meshRevision = assets->contentRevision<MeshAsset>();
    const auto materialRevision = std::max({
        assets->contentRevision<MaterialAsset>(),
        assets->contentRevision<PbrtTextureAsset>(),
        assets->contentRevision<TextureAsset>(),
        assets->contentRevision<SamplerAsset>(),
    });
    const bool itemsChanged = previousItems != source.drawItems;
    const bool materialsNeedUpdate = materialAssetRevision != materialRevision ||
        (itemsChanged && !std::ranges::equal(previousItems, source.drawItems, [](const DrawItem& a, const DrawItem& b)
        {
            return a.materialId == b.materialId && a.materialOverride == b.materialOverride &&
                (!a.materialOverride || (a.selectionId == b.selectionId && a.instanceIndex == b.instanceIndex &&
                    a.submeshIndex == b.submeshIndex));
        }));

    if (itemsChanged || meshAssetRevision != meshRevision)
    {
        if (updateDraws(source.drawItems, meshRevision, commands))
            updates |= SceneUpdateFlags::Geometry;
        meshAssetRevision = meshRevision;
    }

    if (materialsNeedUpdate)
    {
        if (updateMaterials(source.drawItems))
            updates |= SceneUpdateFlags::Materials;
        materialAssetRevision = materialRevision;
    }
    if (itemsChanged)
        previousItems = source.drawItems;

    if (analytic.update(source.analyticLights, *assetData, *device, commands))
        updates |= SceneUpdateFlags::AnalyticLights;
    if (updateEnvironment(source, commands))
        updates |= SceneUpdateFlags::Environment;
    if (materialTable.upload(commands))
        updates |= SceneUpdateFlags::Materials;

    return updates | updateGlobals(source, commands);
}

bool GpuScene::updateDraws(std::span<const DrawItem> items, uint64_t meshRevision, rhi::ICommandEncoder& commands)
{
    const bool assetsChanged = meshAssetRevision != meshRevision;
    bool geometryChanged = items.size() != draws.size();
    bool orientationChanged = false;
    rayHitsDirty |= geometryChanged;
    if (geometryChanged)
        emissive.invalidate();

    draws.resize(items.size());
    for (size_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        const auto* previous = index < previousItems.size() ? &previousItems[index] : nullptr;
        auto& draw = draws[index];
        const bool meshChanged = !previous || previous->mesh != item.mesh || previous->submeshIndex != item.submeshIndex ||
            (assetsChanged && assets->revision(item.mesh) > meshAssetRevision);
        if (meshChanged)
        {
            const auto& asset = assets->get(item.mesh);
            const auto& submesh = asset.submeshes[item.submeshIndex];
            draw.mesh = &resources->loadMesh(item.mesh, commands);
            draw.asset = &asset;
            draw.firstIndex = submesh.firstIndex;
            draw.indexCount = submesh.indexCount;
        }

        const bool placementChanged = meshChanged || previous->model != item.model;
        if (placementChanged)
        {
            draw.model = item.model;
            draw.bounds = draw.asset->bounds.transformed(item.model);
        }

        const bool wasMirrored = draw.mirroredPbrtSurface;
        const bool reverse = item.pbrtSurface && item.pbrtSurface->reverseOrientation;
        if (placementChanged || previous->pbrtSurface.has_value() != item.pbrtSurface.has_value())
            draw.mirroredPbrtSurface = item.pbrtSurface && glm::determinant(glm::mat3{item.model}) < 0.0f;
        const bool reverseChanged = draw.reverseOrientation != reverse;
        const bool drawOrientationChanged = reverseChanged || wasMirrored != draw.mirroredPbrtSurface;
        draw.reverseOrientation = reverse;
        draw.selectionId = item.selectionId;

        geometryChanged |= placementChanged;
        orientationChanged |= drawOrientationChanged;
        rayHitsDirty |= placementChanged || reverseChanged;
        if (previous && (placementChanged || drawOrientationChanged) && materialTable.info(draw.materialIndex).emissive)
            emissive.invalidate();
    }

    if (geometryChanged)
    {
        viewCulled = false;
        acceleration.invalidate();
    }
    return geometryChanged || orientationChanged;
}

bool GpuScene::updateMaterials(std::span<const DrawItem> items)
{
    const auto previousEmissionRevision = materialTable.emissionRevision();
    const auto indices = materialTable.prepare(items, *assets, *assetData);
    if (materialTable.emissionRevision() != previousEmissionRevision)
        emissive.invalidate();
    bool bindingsChanged = false;
    for (size_t index = 0; index < draws.size(); ++index)
    {
        auto& draw = draws[index];
        if (index < previousItems.size() && draw.materialIndex == indices[index])
            continue;

        if ((index < previousItems.size() && materialTable.info(draw.materialIndex).emissive) ||
            materialTable.info(indices[index]).emissive)
            emissive.invalidate();
        draw.materialIndex = indices[index];
        bindingsChanged = true;
    }
    rayHitsDirty |= bindingsChanged;
    return bindingsChanged;
}

bool GpuScene::updateEnvironment(const SceneRenderData& source, rhi::ICommandEncoder& commands)
{
    const bool mapChanged = environmentMapId != source.environmentMap;
    if (mapChanged)
    {
        environmentMapId = source.environmentMap;
        environment = environmentMapId ? &resources->environmentMap(*environmentMapId, commands) : nullptr;
    }
    if (environmentLight == source.environmentLight)
        return mapChanged;

    environmentLight = source.environmentLight;
    environmentData = {};
    if (environmentLight)
    {
        const auto& emission = environmentLight->emission;
        const auto scale = static_cast<float>(emission.scale);
        environmentData.lightFromWorld = environmentLight->lightFromWorld;
        if (emission.image)
        {
            environmentData.image = materialTable.textureReference(emission.image->image);
            environmentData.emission = {glm::vec3{scale}, 0.0f};
        }
        else
            environmentData.emission = {pbrtPreviewRgb(emission.spectrum, *assetData) * scale, 0.0f};
        uploadSceneBuffer(commands, environmentLightBuffer, std::as_bytes(std::span{&environmentData, 1}));
    }
    return true;
}

SceneUpdateFlags GpuScene::updateGlobals(const SceneRenderData& source, rhi::ICommandEncoder& commands)
{
    const auto aspect = source.viewport.width / source.viewport.height;
    const bool cameraChanged = previousCamera != source.camera || aspect != sceneViewport.width / sceneViewport.height;
    auto updates = source.viewport != sceneViewport ? SceneUpdateFlags::Viewport : SceneUpdateFlags::None;
    sceneViewport = source.viewport;
    if (cameraChanged)
    {
        const auto& camera = source.camera;
        const auto view = camera.viewMatrix();
        const auto viewProjection = camera.projectionMatrix(aspect) * view;
        sceneGlobals.camera = {
            .viewProjection        = viewProjection,
            .inverseViewProjection = glm::inverse(viewProjection),
            .cameraPosition        = glm::vec4{camera.position, 1.0f},
            .view                  = view,
        };
        previousCamera = camera;
        viewCulled = false;
        updates |= SceneUpdateFlags::Camera;
    }

    const auto lightCount = static_cast<uint32_t>(analyticLights().size());
    const auto maxLod = environment ? environment->prefilteredMaxLod : 0.0f;
    if (cameraChanged || lightCount != sceneGlobals.analyticLightCount || maxLod != sceneGlobals.prefilteredMaxLod)
    {
        sceneGlobals.analyticLightCount = lightCount;
        sceneGlobals.prefilteredMaxLod = maxLod;
        rhiCheck(commands.uploadBufferData(globals, 0, sizeof(sceneGlobals), &sceneGlobals));
    }
    return updates;
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
    static_assert(
        sizeof(Vertex) == 48 && offsetof(Vertex, normal) == 12 && offsetof(Vertex, texcoord) == 24 &&
        offsetof(Vertex, tangent) == 32
    );
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
            .objectToWorld      = draw.model,
            .worldToObject      = glm::inverse(draw.model),
        };
    }
    uploadSceneBuffer(commands, rayHitBuffer, std::as_bytes(std::span{hits}));
}

void GpuScene::bindShaderData(rhi::IShaderObject& object) const
{
    const auto scene = rhi::ShaderCursor(&object)["scene"].getDereferenced();
    rhiCheck(scene["globals"].setBinding(globals));
    rhiCheck(scene["analyticLights"].setBinding(analytic.buffer()));
}

void GpuScene::bindRayTracingGeometryData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const
{
    bindShaderData(object);
    materialTable.bind(object);
    const auto scene = rhi::ShaderCursor(&object)["scene"].getDereferenced();
    rhiCheck(scene["rtAccel"].setBinding(tlas));
    rhiCheck(scene["objects"].setBinding(rayHitBuffer));
}

void GpuScene::bindRayTracingShaderData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const
{
    bindRayTracingGeometryData(object, tlas);
    const rhi::ShaderCursor root(&object);
    emissive.bind(root["emissiveLights"]);
    if (const auto field = root["environmentLight"]; field.isValid())
        rhiCheck(field.setBinding(environmentLightBuffer));
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
