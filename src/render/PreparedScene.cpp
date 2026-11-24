#include "render/PreparedScene.h"

#include "asset/AssetManager.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/graph/RenderPass.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/RenderResourceManager.h"

#include <cstring>

void PreparedScene::init(rhi::IDevice& device, RenderResourceManager& resources)
{
    lighting.init(device, resources);
    rhiCheck(device.createBuffer(
        {.size = sizeof(SceneGlobals), .usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination},
        nullptr,
        globals.writeRef()
    ));
    uploadedLightRevision.reset();
}

void PreparedScene::prepare(
    const RenderContext&         frame,
    SceneInputs                  sceneInputs,
    const AssetManager&          assets,
    SceneAccelerationStructures& acceleration
)
{
    const auto& camera = frame.view->uniforms;
    drawBounds.clear();
    visibilityMasks.clear();
    if (is_set(sceneInputs, SceneInputs::RasterVisibility))
    {
        const Frustum frustum{camera.viewProjection};
        drawBounds.reserve(drawItems.size());
        visibilityMasks.reserve(drawItems.size());
        for (const auto& item : drawItems)
        {
            const auto worldBounds = assets.get(item.mesh).bounds.transformed(item.model);
            drawBounds.push_back(worldBounds);
            visibilityMasks.push_back(frustum.intersects(worldBounds) ? RenderVisibility::kMainView : 0);
        }
    }

    auto&      cache       = *frame.gpuResources;
    auto&      materials   = *frame.materialTable;
    const auto queueUpload = [&](rhi::IBuffer* buffer, std::span<const std::byte> bytes)
    {
        const auto upload = cache.allocateUpload(cache.registerBuffer(buffer), bytes.size());
        std::memcpy(upload.data.data(), bytes.data(), bytes.size());
    };
    const SceneGlobals sceneGlobals{
        .camera            = camera,
        .lightRanges       = lightRanges(),
        .prefilteredMaxLod = environmentMap ? environmentMap->prefilteredMaxLod : 0.0f,
    };
    queueUpload(globals, std::as_bytes(std::span{&sceneGlobals, 1}));
    if (uploadedLightRevision != shading->revision)
    {
        if (!lights().empty())
            queueUpload(lighting.getLightBuffer(), std::as_bytes(lights()));
        uploadedLightRevision = shading->revision;
    }

    materials.upload();
    materials.uploadPrograms(shading->materials, shading->textures, shading->revision);
    const bool rayTracing = is_set(sceneInputs, SceneInputs::RayTracing);
    if (rayTracing)
        rays.upload(*this, *frame.resources);
    cache.recordUploads(*frame.commands);

    tlas = rayTracing
        ? acceleration.recordBuilds(*frame.device, *frame.commands, *frame.resources, *this, frame.frameIndex)
        : nullptr;
}
