#pragma once

#include "asset/EnvironmentMapAsset.h"
#include "core/utils/NonCopyable.h"
#include "render/resources/AnalyticLights.h"
#include "render/resources/EmissiveLights.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/SceneAccelerationStructures.h"
#include "render/resources/ShaderData.h"
#include "scene/Scene.h"

#include <cstddef>
#include <cstdint>
#include <glm/vec2.hpp>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

struct GpuEnvironmentMap;
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
SLANG_RHI_ENUM_CLASS_OPERATORS(SceneUpdateFlags);

inline const auto kSceneShadingUpdates = SceneUpdateFlags::Scene | SceneUpdateFlags::Geometry |
    SceneUpdateFlags::Materials | SceneUpdateFlags::AnalyticLights | SceneUpdateFlags::Environment;

struct GpuDraw
{
    const GpuMesh*   mesh;
    const MeshAsset* asset;
    glm::mat4        model;
    uint32_t         matrixID;
    uint32_t         firstIndex;
    uint32_t         indexCount;
    uint32_t         materialIndex;
};

class GpuScene : private NonCopyable
{
public:
    void init(
        rhi::IDevice&          device,
        const AssetManager&    assets,
        RenderResourceManager& resources,
        ShaderManager&         shaders
    );
    SceneUpdateFlags update(
        const Scene& source, uint64_t sceneGeneration, glm::uvec2 extent, rhi::ICommandEncoder& commands
    );

    void bindSceneData(rhi::IShaderObject& object, rhi::IAccelerationStructure* tlas) const;
    // Record before opening a ray tracing pass.
    [[nodiscard]] rhi::IAccelerationStructure* prepareRayTracing(rhi::ICommandEncoder& commands);
    void prepareEmissiveLights(rhi::ICommandEncoder& commands);
    void raytrace(
        rhi::ICommandEncoder&     commands,
        rhi::IRayTracingPipeline& pipeline,
        rhi::IShaderTable&        shaderTable,
        rhi::IShaderObject&       shaderObject,
        rhi::Extent3D             extent
    );

    [[nodiscard]] uint64_t sceneGeneration() const { return generation; }
    [[nodiscard]] uint32_t analyticLightCount() const { return lightCount; }
    [[nodiscard]] bool hasEnvironment() const { return environment != nullptr; }
    [[nodiscard]] bool useAnalyticLights() const { return !analytic.data().empty(); }
    [[nodiscard]] bool useEmissiveLights() const { return emissive.count() != 0; }

private:
    struct MeshBufferSpan
    {
        uint32_t geometryID;
        uint32_t vbOffset;
        uint32_t ibBaseOffset;
    };

    SceneUpdateFlags updateObjects(std::span<const Object> objects, rhi::ICommandEncoder& commands);
    bool updateEnvironment(const Scene& source, rhi::ICommandEncoder& commands);
    SceneUpdateFlags updateGlobals(const Camera& camera, glm::uvec2 extent);
    void uploadGeometryBuffers(rhi::ICommandEncoder& commands);
    void uploadInstanceData(rhi::ICommandEncoder& commands);
    void uploadSceneBuffer(
        rhi::ICommandEncoder&      commands,
        rhi::ComPtr<rhi::IBuffer>& buffer,
        std::span<const std::byte> bytes
    );

    rhi::IDevice*          device    = nullptr;
    const AssetManager*    assets    = nullptr;
    RenderResourceManager* resources = nullptr;
    ShaderManager*         shaders   = nullptr;

    uint64_t                               generation = 0;
    std::optional<EnvironmentMapAsset::ID> environmentMapId;
    const GpuEnvironmentMap*               environment = nullptr;
    std::vector<GpuDraw>                   draws;
    std::vector<glm::mat4>                 objectModels;
    std::vector<Object>                    previousObjects;
    uint64_t                               meshAssetRevision = 0;
    std::optional<uint64_t>                materialAssetRevision;
    std::optional<Camera>                  previousCamera;
    float                                  cameraAspect = 1.0f;

    CameraData                  cameraData;
    uint32_t                    lightCount = 0;
    AnalyticLights              analytic;
    EmissiveLights              emissive;
    MaterialTable               materialTable;
    SceneAccelerationStructures acceleration;

    std::unordered_map<const GpuMesh*, MeshBufferSpan> meshSpans;
    rhi::ComPtr<rhi::IBuffer>                          vertexBuffer;
    rhi::ComPtr<rhi::IBuffer>                          indexBuffer;
    rhi::ComPtr<rhi::IBuffer>                          worldMatrixBuffer;
    rhi::ComPtr<rhi::IBuffer>                          inverseTransposeWorldMatrixBuffer;
    rhi::ComPtr<rhi::IBuffer>                          geometryInstanceBuffer;
    bool                                               geometryBuffersDirty = true;
    bool                                               instanceBuffersDirty = true;
};
