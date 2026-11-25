#pragma once

#include "core/math/Bounds.h"
#include "asset/EnvironmentMapAsset.h"
#include "core/Macros.h"
#include "render/DrawItem.h"
#include "render/ViewportRect.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/resources/AnalyticLights.h"
#include "render/resources/EmissiveLights.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderData.h"
#include "scene/system/SceneCamera.h"

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
class ShaderManager;

enum class SceneUpdateFlags : uint32_t
{
    None           = 0,
    Scene          = 1 << 0,
    Camera         = 1 << 1,
    Geometry       = 1 << 2,
    Materials      = 1 << 3,
    AnalyticLights = 1 << 4,
    Environment    = 1 << 5,
};
RL_ENUM_CLASS_OPERATORS(SceneUpdateFlags);

inline constexpr auto kSceneShadingUpdates = SceneUpdateFlags::Scene | SceneUpdateFlags::Geometry |
    SceneUpdateFlags::Materials | SceneUpdateFlags::AnalyticLights | SceneUpdateFlags::Environment;

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
    bool mirroredPbrtSurface = false;
    bool reverseOrientation = false;
};

class GpuScene
{
public:
    GpuScene()                      = default;
    GpuScene(GpuScene&&)            = default;
    GpuScene& operator=(GpuScene&&) = default;

    void init(
        rhi::IDevice&           device,
        const AssetManager&     assets,
        const AssetDataManager& assetData,
        RenderResourceManager&  resources,
        ShaderManager&          shaders
    );
    SceneUpdateFlags update(const SceneRenderData& source, Extent2D extent, rhi::ICommandEncoder& commands);

    void bindShaderData(rhi::IShaderObject& object) const;
    // Record before opening a ray tracing pass.
    [[nodiscard]] rhi::IAccelerationStructure* prepareRayTracing(rhi::ICommandEncoder& commands);
    void prepareEmissiveLights(rhi::ICommandEncoder& commands);
    void bindRayTracingGeometryData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const;
    void bindRayTracingShaderData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const;
    void raytrace(
        rhi::ICommandEncoder& commands,
        rhi::IRayTracingPipeline& pipeline,
        rhi::IShaderTable& shaderTable,
        rhi::IShaderObject& shaderObject,
        rhi::Extent3D extent
    );

    [[nodiscard]] uint64_t                  sceneGeneration() const { return generation; }
    [[nodiscard]] std::span<const GpuDraw>   drawCalls() const { return draws; }
    [[nodiscard]] std::span<const uint32_t> visibleDraws();
    [[nodiscard]] const SceneUniforms&      cameraUniforms() const { return sceneGlobals.camera; }
    [[nodiscard]] const GpuEnvironmentMap*  environmentMap() const { return environment; }
    [[nodiscard]] bool hasEnvironment() const { return environment != nullptr || environmentLight.has_value(); }
    [[nodiscard]] glm::vec3            environmentEmission() const { return glm::vec3{environmentData.emission}; }
    [[nodiscard]] const MaterialTable& materials() const { return materialTable; }
    [[nodiscard]] std::span<const LightData> analyticLights() const { return analytic.data(); }
    [[nodiscard]] const EmissiveLights& emissiveLights() const { return emissive; }
    [[nodiscard]] bool useAnalyticLights() const { return !analytic.data().empty(); }
    [[nodiscard]] bool useEmissiveLights() const { return emissive.count() != 0; }
    [[nodiscard]] rhi::ITextureView*         brdfLut() const { return brdfLutView; }
    [[nodiscard]] rhi::ITextureView*         sheenLut() const { return sheenLutView; }
    [[nodiscard]] rhi::ISampler*             lightingLutSampler() const { return lutSampler; }

private:
    bool updateDraws(std::span<const DrawItem> items, uint64_t meshRevision, rhi::ICommandEncoder& commands);
    bool updateMaterials(std::span<const DrawItem> items);
    bool updateEnvironment(const SceneRenderData& source, rhi::ICommandEncoder& commands);
    SceneUpdateFlags updateGlobals(const SceneRenderData& source, Extent2D extent);
    void uploadRayHits(rhi::ICommandEncoder& commands);
    void uploadSceneBuffer(
        rhi::ICommandEncoder& commands,
        rhi::ComPtr<rhi::IBuffer>& buffer,
        std::span<const std::byte> bytes
    );

    rhi::IDevice*           device    = nullptr;
    const AssetManager*     assets    = nullptr;
    const AssetDataManager* assetData = nullptr;
    RenderResourceManager*  resources = nullptr;
    ShaderManager*          shaders = nullptr;

    uint64_t                    generation  = 0;
    std::optional<EnvironmentMapAsset::ID> environmentMapId;
    const GpuEnvironmentMap*    environment = nullptr;
    std::optional<SceneEnvironmentLight> environmentLight;
    std::vector<GpuDraw>        draws;
    std::vector<DrawItem>       previousItems;
    uint64_t                    meshAssetRevision = 0;
    std::optional<uint64_t>     materialAssetRevision;
    std::optional<SceneCamera>  previousCamera;
    float                       cameraAspect = 1.0f;
    std::vector<uint32_t>       visibleDrawIndices;
    bool                        viewCulled = false;

    SceneGlobals                   sceneGlobals;
    EnvironmentLightData           environmentData;
    AnalyticLights                 analytic;
    EmissiveLights                 emissive;
    MaterialTable                  materialTable;
    SceneAccelerationStructures    acceleration;
    rhi::ComPtr<rhi::IBuffer>      rayHitBuffer;
    rhi::ComPtr<rhi::IBuffer>      environmentLightBuffer;
    rhi::ComPtr<rhi::ITextureView> blackEnvironmentView;
    rhi::ComPtr<rhi::ITextureView> brdfLutView, sheenLutView;
    rhi::ISampler*                 lutSampler = nullptr;
    bool                           rayHitsDirty = true;
};
