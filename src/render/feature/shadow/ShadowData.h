#pragma once
#include <span>
#include <vector>
#include "render/RendererConfig.h"
#include "render/resource/ShaderData.h"

struct RenderLight;

// CPU output of shadow preparation, independent of GPU allocation and upload.
struct ShadowFrameData
{
    bool                               operator==(const ShadowFrameData&) const = default;
    std::vector<LightData>             lights;
    LightRanges                        lightRanges;
    std::vector<PointShadowData>       pointShadows;
    std::vector<SpotShadowData>        spotShadows;
    std::vector<RectAreaShadowData>    rectAreaShadows;
    std::vector<DirectionalShadowData> directionalShadows;
};
ShadowFrameData
prepareShadows(const SceneUniforms& view, std::span<const RenderLight> lights, const RenderFeature::Shadow& settings);
