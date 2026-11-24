#pragma once

#include "core/math/Bounds.h"
#include "render/DrawItem.h"
#include "render/ViewportRect.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/SceneShading.h"
#include "render/resources/ShaderData.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

struct GpuEnvironmentMap;
struct SceneRenderData;
class AssetDataManager;
class AssetManager;
class GpuMesh;
class RenderResourceManager;

struct GpuDraw
{
    const GpuMesh* mesh;
    const MeshAsset* asset;
    glm::mat4 model;
    Aabb bounds;
    uint32_t firstIndex;
    uint32_t indexCount;
    uint32_t materialIndex;
    uint32_t selectionId;
    uint32_t firstAreaLight = kInvalidLightIndex;
    bool mirroredPbrtSurface = false;
    bool reverseOrientation = false;
};

class GpuScene
{
public:
    class RayTracingData
    {
    public:
        void                                  bind(rhi::IShaderObject* object) const;
        [[nodiscard]] rhi::IBuffer*           triangleLightBuffer() const { return scene.triangleLights; }
        [[nodiscard]] const SceneShadingData& shading() const { return scene.sceneShading.data(); }

    private:
        friend class GpuScene;
        RayTracingData(const GpuScene& scene, rhi::IAccelerationStructure* tlas) : scene(scene), tlas(tlas) {}

        const GpuScene&              scene;
        rhi::IAccelerationStructure* tlas;
    };

    GpuScene()                      = default;
    GpuScene(GpuScene&&)            = default;
    GpuScene& operator=(GpuScene&&) = default;

    void init(
        rhi::IDevice&           device,
        const AssetManager&     assets,
        const AssetDataManager& assetData,
        RenderResourceManager&  resources
    );
    // Resolve assets before acquiring a swapchain image. Submit the recorded uploads even without a back buffer.
    void update(const SceneRenderData& source, rhi::ICommandEncoder& commands);

    // Record before opening a ray tracing pass. The returned data is borrowed for this frame.
    [[nodiscard]] RayTracingData prepareRayTracing(rhi::ICommandEncoder& commands, uint32_t frameIndex);

    [[nodiscard]] uint64_t                  sceneGeneration() const { return generation; }
    [[nodiscard]] std::span<const GpuDraw>   drawCalls() const { return draws; }
    [[nodiscard]] std::span<const uint32_t> visibleDraws();
    [[nodiscard]] const SceneUniforms&      cameraUniforms() const { return sceneGlobals.camera; }
    [[nodiscard]] const ViewportRect&       viewport() const { return sceneViewport; }
    [[nodiscard]] const GpuEnvironmentMap*  environmentMap() const { return environment; }
    [[nodiscard]] uint64_t                  shadingRevision() const { return sceneShading.data().revision; }
    [[nodiscard]] bool                 hasEnvironmentLight() const { return sceneShading.data().hasEnvironmentLight; }
    [[nodiscard]] const MaterialTable& materials() const { return materialTable; }
    [[nodiscard]] std::span<const LightData> lights() const { return sceneShading.data().lights; }
    [[nodiscard]] const LightRanges&         lightRanges() const { return sceneShading.data().lightRanges; }
    [[nodiscard]] rhi::IBuffer*              globalsBuffer() const { return globals; }
    [[nodiscard]] rhi::IBuffer*              lightBuffer() const { return lightDataBuffer; }
    [[nodiscard]] rhi::ITextureView*         brdfLut() const { return brdfLutView; }
    [[nodiscard]] rhi::ITextureView*         sheenLut() const { return sheenLutView; }
    [[nodiscard]] rhi::ISampler*             lightingLutSampler() const { return lutSampler; }

private:
    void uploadRayTracingData(rhi::ICommandEncoder& commands);

    rhi::IDevice*           device    = nullptr;
    const AssetManager*     assets    = nullptr;
    const AssetDataManager* assetData = nullptr;
    RenderResourceManager*  resources = nullptr;

    uint64_t                    generation  = 0;
    const GpuEnvironmentMap*    environment = nullptr;
    std::vector<GpuDraw>        draws;
    std::vector<DrawItem>       previousItems;
    std::optional<uint64_t>     preparedAssetRevision;
    std::vector<SceneLight>     previousLights;
    std::vector<uint32_t>       visibleDrawIndices;
    bool                        viewCulled = false;

    ViewportRect                   sceneViewport;
    SceneGlobals                   sceneGlobals;
    SceneShading                   sceneShading;
    MaterialTable                  materialTable;
    SceneAccelerationStructures    acceleration;
    rhi::ComPtr<rhi::IBuffer>      globals;
    rhi::ComPtr<rhi::IBuffer>      lightDataBuffer;
    rhi::ComPtr<rhi::IBuffer>      rayHitBuffer;
    rhi::ComPtr<rhi::IBuffer>      triangleLights;
    rhi::ComPtr<rhi::IBuffer>      areaLightEmissions;
    rhi::ComPtr<rhi::IBuffer>      environmentLightBuffer;
    rhi::ComPtr<rhi::ITextureView> brdfLutView, sheenLutView;
    rhi::ISampler*                 lutSampler = nullptr;
    std::optional<uint64_t>        uploadedLightRevision;
    std::optional<uint64_t>        uploadedRayRevision;
    rhi::IAccelerationStructure*   frameTlas = nullptr;
};
