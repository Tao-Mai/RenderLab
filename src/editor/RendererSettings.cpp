#include "editor/RendererSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/matrix.hpp>
#include <imgui.h>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "editor/Inspector.h"
#include "render/Renderer.h"
#include "render/device/VulkanContext.h"
#include "render/resources/ShaderData.h"

namespace
{
bool drawSettingsHeader(const char* label)
{
    bool        reset      = false;
    const float buttonSize = ImGui::GetFrameHeight();
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginTable("Header", 2, ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, buttonSize);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SeparatorText(label);
        ImGui::TableNextColumn();
        reset = ImGui::Button("##ResetDefaults", ImVec2(buttonSize, buttonSize));
        ImGui::SetItemTooltip("Restore default parameters");

        const ImVec2    min = ImGui::GetItemRectMin();
        const ImVec2    center(min.x + buttonSize * 0.5f, min.y + buttonSize * 0.5f);
        const float     radius      = buttonSize * 0.23f;
        constexpr float kStartAngle = std::numbers::pi_v<float> * 0.15f;
        constexpr float kEndAngle   = std::numbers::pi_v<float> * 1.75f;
        const ImVec2    radial(std::cos(kEndAngle), std::sin(kEndAngle));
        const ImVec2    tangent(-radial.y, radial.x);
        const ImVec2    end(center.x + radial.x * radius, center.y + radial.y * radius);
        const float     arrowSize = radius * 0.65f;
        const ImU32     color     = ImGui::GetColorU32(ImGuiCol_Text);
        auto*           drawList  = ImGui::GetWindowDrawList();
        drawList->PathArcTo(center, radius, kStartAngle, kEndAngle);
        drawList->PathStroke(color, 0, std::max(1.0f, buttonSize * 0.06f));
        drawList->AddTriangleFilled(
            ImVec2(end.x + tangent.x * arrowSize, end.y + tangent.y * arrowSize),
            ImVec2(end.x + radial.x * arrowSize, end.y + radial.y * arrowSize),
            ImVec2(end.x - radial.x * arrowSize, end.y - radial.y * arrowSize),
            color
        );
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return reset;
}

bool drawEnvironmentSelection(SceneAsset& scene, AssetManager& assets)
{
    bool edited = false;
    if (drawSettingsHeader("Environment") && scene.environmentMap)
    {
        scene.environmentMap = SceneAsset{}.environmentMap;
        edited               = true;
    }
    auto&       current = scene.environmentMap;
    const char* preview = current ? current->value.c_str() : "Select environment map...";

    if (ImGui::BeginCombo("Environment map", preview))
    {
        auto ids = assets.loadedIds<EnvironmentMapAsset>();
        std::ranges::sort(ids, {}, &EnvironmentMapAsset::ID::value);
        for (const EnvironmentMapAsset::ID& id : ids)
        {
            const bool selected = current && *current == id;
            if (ImGui::Selectable(id.value.c_str(), selected) && !selected)
            {
                current = id;
                edited  = true;
            }
        }
        ImGui::EndCombo();
    }
    if (!current)
        ImGui::TextUnformatted("Using fallback environment lighting");
    return edited;
}

bool drawFalcorPostProcessSettings(FalcorPostProcessConfig& settings)
{
    using namespace falcorPostProcess;
    bool  edited     = false;
    auto& toneMapper = settings.toneMapper;
    if (drawSettingsHeader("Falcor ToneMapper"))
    {
        toneMapper = {};
        edited     = true;
    }
    ImGui::TextDisabled("Falcor exposure is independent of camera exposure.");
    edited |= ImGui::SliderFloat(
        "Exposure Compensation",
        &toneMapper.exposureCompensation,
        kExposureCompensationMin,
        kExposureCompensationMax,
        "%.1f"
    );
    edited |= ImGui::Checkbox("Auto Exposure", &toneMapper.autoExposure);
    if (!toneMapper.autoExposure)
    {
        int exposureMode = static_cast<int>(toneMapper.exposureMode);
        if (ImGui::Combo("Exposure mode", &exposureMode, "Aperture priority\0Shutter priority\0"))
        {
            toneMapper.exposureMode = static_cast<FalcorExposureMode>(exposureMode);
            edited                  = true;
        }
        float       ev    = toneMapper.exposureValue();
        const float minEv = std::log2(kShutterMin * kFNumberMin * kFNumberMin);
        const float maxEv = std::log2(kShutterMax * kFNumberMax * kFNumberMax);
        if (ImGui::SliderFloat("Exposure Value (EV)", &ev, minEv, maxEv, "%.1f"))
        {
            toneMapper.setExposureValue(ev);
            edited = true;
        }
        edited |= ImGui::DragFloat(
            "Film Speed (ISO)",
            &toneMapper.filmSpeed,
            1.0f,
            kFilmSpeedMin,
            kFilmSpeedMax,
            "%.1f",
            ImGuiSliderFlags_AlwaysClamp
        );
        edited |= ImGui::DragFloat(
            "f-Number",
            &toneMapper.fNumber,
            0.1f,
            kFNumberMin,
            kFNumberMax,
            "%.1f",
            ImGuiSliderFlags_AlwaysClamp
        );
        edited |= ImGui::DragFloat(
            "Shutter",
            &toneMapper.shutter,
            0.1f,
            kShutterMin,
            kShutterMax,
            "%.1f",
            ImGuiSliderFlags_AlwaysClamp
        );
        ImGui::SetItemTooltip("Reciprocal shutter time (1/seconds), matching Falcor.");
    }
    edited |= ImGui::Checkbox("White Balance", &toneMapper.whiteBalance);
    if (toneMapper.whiteBalance)
    {
        edited |= ImGui::DragFloat(
            "White Point (K)",
            &toneMapper.whitePoint,
            5.0f,
            kWhitePointMin,
            kWhitePointMax,
            "%.0f",
            ImGuiSliderFlags_AlwaysClamp
        );
        auto white = glm::inverse(toneMapper.whiteBalanceTransform()) * glm::vec3{1.0f};
        white /= std::max({white.r, white.g, white.b});
        ImGui::ColorButton("Source white", ImVec4{white.r, white.g, white.b, 1.0f});
    }
    int mode = static_cast<int>(toneMapper.mode);
    if (ImGui::Combo("Operator", &mode, "Linear\0Reinhard\0Reinhard Modified\0Heji Hable ALU\0Hable UC2\0ACES\0"))
    {
        toneMapper.mode = static_cast<FalcorToneMappingOperator>(mode);
        edited          = true;
    }
    if (toneMapper.mode == FalcorToneMappingOperator::ReinhardModified)
        edited |= ImGui::DragFloat(
            "White Luminance",
            &toneMapper.whiteMaxLuminance,
            0.2f,
            0.1f,
            std::numeric_limits<float>::max(),
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        );
    if (toneMapper.mode == FalcorToneMappingOperator::HableUc2)
        edited |= ImGui::DragFloat(
            "Linear White",
            &toneMapper.whiteScale,
            0.01f,
            0.001f,
            100.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        );
    edited |= ImGui::Checkbox("Clamp Output", &toneMapper.clampOutput);

    auto& postFx = settings.simplePostFx;
    ImGui::PushID("FalcorSimplePostFX");
    if (drawSettingsHeader("Falcor SimplePostFX"))
    {
        postFx = {};
        edited = true;
    }
    edited |= ImGui::Checkbox("Enable post fx", &postFx.enabled);
    edited |= ImGui::SliderFloat("Wipe", &postFx.wipe, 0.0f, 1.0f);
    if (ImGui::TreeNodeEx("Lens FX", ImGuiTreeNodeFlags_DefaultOpen))
    {
        edited |= ImGui::SliderFloat("Bloom", &postFx.bloomAmount, 0.0f, 1.0f);
        edited |= ImGui::SliderFloat("Bloom Star", &postFx.starAmount, 0.0f, 1.0f);
        edited |= ImGui::SliderFloat("Star Angle", &postFx.starAngle, 0.0f, 1.0f);
        edited |= ImGui::SliderFloat("Vignette", &postFx.vignetteAmount, 0.0f, 1.0f);
        edited |= ImGui::SliderFloat("Chromatic Aberration", &postFx.chromaticAberrationAmount, 0.0f, 1.0f);
        edited |= ImGui::SliderFloat("Barrel Distortion", &postFx.barrelDistortAmount, 0.0f, 1.0f);
        if (ImGui::Button("Reset lens FX"))
        {
            const FalcorPostFxConfig defaults;
            postFx.bloomAmount               = defaults.bloomAmount;
            postFx.starAmount                = defaults.starAmount;
            postFx.starAngle                 = defaults.starAngle;
            postFx.vignetteAmount            = defaults.vignetteAmount;
            postFx.chromaticAberrationAmount = defaults.chromaticAberrationAmount;
            postFx.barrelDistortAmount       = defaults.barrelDistortAmount;
            edited                           = true;
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Saturation"))
    {
        edited |= ImGui::SliderFloat("Shadow Saturation", &postFx.saturationCurve.x, 0.0f, 2.0f);
        edited |= ImGui::SliderFloat("Midtone Saturation", &postFx.saturationCurve.y, 0.0f, 2.0f);
        edited |= ImGui::SliderFloat("Highlight Saturation", &postFx.saturationCurve.z, 0.0f, 2.0f);
        if (ImGui::Button("Reset saturation"))
        {
            postFx.saturationCurve = FalcorPostFxConfig{}.saturationCurve;
            edited                 = true;
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Offset/Power/Scale (luma)"))
    {
        edited |= ImGui::SliderFloat("Luma Offset (Shadows)", &postFx.colorOffsetScalar, -1.0f, 1.0f);
        edited |= ImGui::SliderFloat("Luma Power (Midtones)", &postFx.colorPowerScalar, -1.0f, 1.0f);
        edited |= ImGui::SliderFloat("Luma Scale (Highlights)", &postFx.colorScaleScalar, -1.0f, 1.0f);
        if (ImGui::Button("Reset luma"))
        {
            postFx.colorOffsetScalar = 0.0f;
            postFx.colorScaleScalar  = 0.0f;
            postFx.colorPowerScalar  = 0.0f;
            edited                   = true;
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Offset/Power/Scale (color)"))
    {
        edited |= ImGui::ColorEdit3("Color Offset (Shadows)", &postFx.colorOffset.x);
        edited |= ImGui::ColorEdit3("Color Power (Midtones)", &postFx.colorPower.x);
        edited |= ImGui::ColorEdit3("Color Scale (Highlights)", &postFx.colorScale.x);
        if (ImGui::Button("Reset color"))
        {
            const FalcorPostFxConfig defaults;
            postFx.colorOffset = defaults.colorOffset;
            postFx.colorScale  = defaults.colorScale;
            postFx.colorPower  = defaults.colorPower;
            edited             = true;
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
    return edited;
}

template <class Enum>
bool drawEnumCombo(const char* label, Enum& value, const char* items)
{
    int index = static_cast<int>(value);
    if (!ImGui::Combo(label, &index, items))
        return false;

    value = static_cast<Enum>(index);
    return true;
}

bool drawUnsignedSlider(
    const char*      label,
    uint32_t&        value,
    uint32_t         minimum,
    uint32_t         maximum,
    ImGuiSliderFlags flags = ImGuiSliderFlags_None
)
{
    int selected = static_cast<int>(value);
    if (!ImGui::SliderInt(label, &selected, static_cast<int>(minimum), static_cast<int>(maximum), "%d", flags))
        return false;

    value = static_cast<uint32_t>(selected);
    return true;
}

RenderPath drawRealtimeRenderPath(RenderPath path, bool supportsRayTracing)
{
    constexpr std::array kRenderPaths{
        std::pair{RenderPath::Forward, "Forward"},
        std::pair{RenderPath::Deferred, "Deferred"},
        std::pair{RenderPath::RayTracing, "Real-time ray tracing"},
    };
    const auto selected =
        std::ranges::find_if(kRenderPaths, [path](const auto& option) { return option.first == path; });
    DCHECK(selected != kRenderPaths.end());

    if (!ImGui::BeginCombo("Render path", selected->second))
        return path;

    auto nextPath = path;
    for (const auto& [candidate, label] : kRenderPaths)
    {
        ImGui::BeginDisabled(candidate == RenderPath::RayTracing && !supportsRayTracing);
        if (ImGui::Selectable(label, path == candidate))
            nextPath = candidate;
        ImGui::EndDisabled();
    }
    if (!supportsRayTracing)
        ImGui::TextUnformatted("Ray tracing is not supported by this GPU");
    ImGui::EndCombo();
    return nextPath;
}

bool drawToneMappingSettings(RenderFeature::ToneMapping& toneMapping, bool cameraExposureOverride)
{
    bool edited = drawEnumCombo("Operator", toneMapping.mode, "Khronos PBR Neutral\0Reinhard\0");

    ImGui::TextUnformatted("Global exposure");
    ImGui::PushID("GlobalExposure");
    edited |= drawExposureSettings(toneMapping.exposure);
    ImGui::PopID();

    if (cameraExposureOverride)
        ImGui::TextDisabled("Exposure overridden by Editor camera");
    return edited;
}

bool drawBloomSettings(RenderFeature::Bloom& bloom, bool hdrEnabled)
{
    bool edited = false;
    if (drawSettingsHeader("Bloom"))
    {
        bloom  = {};
        edited = true;
    }

    ImGui::BeginDisabled(!hdrEnabled);
    edited |= ImGui::Checkbox("Enable bloom", &bloom.enable);
    ImGui::SetItemTooltip("HDR scattering bloom; light markers are included, editor UI is excluded. Requires HDR.");

    ImGui::BeginDisabled(!bloom.enable);
    edited |= ImGui::SliderFloat("Bloom intensity", &bloom.intensity, 0.0f, 1.0f, "%.3f");
    ImGui::SetItemTooltip("Fraction of scene light redistributed by bloom, not additive brightness.");
    edited |= ImGui::SliderFloat("Bloom scatter", &bloom.scatter, 0.05f, 0.95f, "%.2f");
    ImGui::SetItemTooltip("Higher values give coarser pyramid levels more weight and produce a wider glow.");
    edited |= ImGui::DragFloat(
        "Gaussian radius",
        &bloom.filterRadius,
        0.05f,
        0.1f,
        5.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );
    edited |= drawUnsignedSlider("Bloom levels", bloom.levels, 1, kMaxBloomLevels);
    ImGui::EndDisabled();

    ImGui::EndDisabled();
    return edited;
}

bool drawPostProcessSettings(RenderFeature& feature, bool cameraExposureOverride)
{
    if (!ImGui::CollapsingHeader("Post Process"))
        return false;

    bool       edited = drawEnumCombo("Post process", feature.postProcess, "RenderLab\0Falcor\0");
    const bool falcor = feature.postProcess == PostProcessMode::Falcor;

    if (drawSettingsHeader("HDR"))
    {
        feature.hdr = RenderFeature{}.hdr;
        if (falcor)
            feature.falcorPostProcess.toneMapper = {};
        else
            feature.toneMapping = {};
        edited = true;
    }
    edited |= ImGui::Checkbox("Enable HDR", &feature.hdr);

    ImGui::BeginDisabled(!feature.hdr);
    if (falcor)
        edited |= drawFalcorPostProcessSettings(feature.falcorPostProcess);
    else
        edited |= drawToneMappingSettings(feature.toneMapping, cameraExposureOverride);
    ImGui::EndDisabled();

    if (!falcor)
        edited |= drawBloomSettings(feature.bloom, feature.hdr);
    return edited;
}

bool drawMaxBounces(uint32_t& maxBounces)
{
    const bool edited =
        drawUnsignedSlider("Max bounces", maxBounces, kMinPathBounces, kMaxPathBounces, ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}

bool drawOfflineSettings(RenderFeature::Offline& offline, const Renderer& renderer)
{
    bool edited = false;
    if (drawSettingsHeader("Offline rendering"))
    {
        offline = {};
        edited  = true;
    }

    ImGui::Text("Samples per pixel: %.2f", renderer.offlineSampleProgress());
    if (renderer.offlinePreparingPipeline())
        ImGui::TextUnformatted("Preparing renderer...");

    edited |= drawEnumCombo("Algorithm", offline.algorithm, "PT\0BDPT\0");
    auto& maxBounces = offline.algorithm == OfflineAlgorithm::PT ? offline.pt.maxBounces : offline.bdpt.maxBounces;
    edited |= drawMaxBounces(maxBounces);

    if (offline.algorithm == OfflineAlgorithm::BDPT)
    {
        auto& bdpt = offline.bdpt;
        edited |= ImGui::Checkbox("Light tracing", &bdpt.lightTracing);
        ImGui::SetItemTooltip("Connect light subpaths to the camera, including specular caustics.");
        edited |= drawEnumCombo("Light selection", bdpt.lightSelection, "Uniform\0Power\0");
        edited |= drawEnumCombo("MIS heuristic", bdpt.misHeuristic, "Balance\0Power (2)\0");
    }

    ImGui::TextUnformatted("Scene, camera or sampling changes reset accumulation.");
    return edited;
}

bool drawRayTracingSettings(RenderFeature::RayTracing& rayTracing)
{
    bool edited = false;
    if (drawSettingsHeader("Ray tracing"))
    {
        rayTracing = {};
        edited     = true;
    }

    edited |= drawEnumCombo("Tracing algorithm", rayTracing.algorithm, "ReSTIR\0Simple\0");
    ImGui::SetItemTooltip(
        "Simple: ordinary light sampling and BRDF bounces. ReSTIR: reservoir-based direct lighting and reuse."
    );
    edited |= drawMaxBounces(rayTracing.maxBounces);

    edited |= ImGui::Checkbox("Enable SVGF", &rayTracing.svgf);
    ImGui::SetItemTooltip(
        "Temporally accumulates direct and indirect lighting, estimates variance and applies\n"
        "five edge-aware wavelet levels. The final level combines the result with emission."
    );

    ImGui::BeginDisabled(!rayTracing.svgf);
    edited |= drawUnsignedSlider(
        "History length",
        rayTracing.historyLength,
        1,
        kMaxSvgfHistoryLength,
        ImGuiSliderFlags_AlwaysClamp
    );
    ImGui::SetItemTooltip(
        "Average the first N samples arithmetically; use EMA when the sample count exceeds N.\n"
        "Applies to color and both luminance moments for direct and indirect lighting."
    );

    edited |=
        ImGui::SliderFloat("EMA sample weight", &rayTracing.emaAlpha, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip(
        "After history length: new mean = history * (1 - weight) + current sample * weight.\n"
        "The first sample, or a sample with rejected history, has weight 1."
    );
    ImGui::EndDisabled();
    return edited;
}

bool drawRestirSettings(RenderFeature::Restir& restir)
{
    bool edited = false;
    if (drawSettingsHeader("ReSTIR"))
    {
        restir = {};
        edited = true;
    }

    edited |= drawUnsignedSlider(
        "Light candidates (M)",
        restir.candidateCount,
        1,
        kMaxRestirCandidateCount,
        ImGuiSliderFlags_AlwaysClamp
    );
    ImGui::SetItemTooltip(
        "Light candidates plus one BRDF candidate per surface. Primary direct lighting uses ReSTIR DI."
    );

    edited |= ImGui::Checkbox("Temporal reuse", &restir.temporalReuse);
    edited |= ImGui::Checkbox("Spatial reuse", &restir.spatialReuse);
    edited |= drawUnsignedSlider(
        "Reservoir history length",
        restir.maxHistoryLength,
        1,
        kMaxRestirHistoryLength,
        ImGuiSliderFlags_AlwaysClamp
    );
    edited |= drawUnsignedSlider(
        "Spatial neighbors",
        restir.spatialSampleCount,
        1,
        kMaxRestirSpatialSamples,
        ImGuiSliderFlags_AlwaysClamp
    );
    edited |= ImGui::SliderFloat(
        "Spatial radius",
        &restir.spatialRadius,
        1.0f,
        kMaxRestirSpatialRadius,
        "%.1f px",
        ImGuiSliderFlags_AlwaysClamp
    );
    return edited;
}

bool drawShadowMapSize(const char* label, uint32_t& mapSize, const vk::PhysicalDeviceLimits& limits, bool cube)
{
    const std::string shadowSize = std::to_string(mapSize);
    if (!ImGui::BeginCombo(label, shadowSize.c_str()))
        return false;

    constexpr uint32_t kShadowMapSizes[] = {256, 512, 1024, 2048, 4096, 8192};
    bool               edited            = false;
    for (uint32_t size : kShadowMapSizes)
    {
        if ((cube && size > limits.maxImageDimensionCube) || size > limits.maxImageDimension2D ||
            size > limits.maxFramebufferWidth || size > limits.maxFramebufferHeight)
            continue;

        const bool        selected = mapSize == size;
        const std::string option   = std::to_string(size);
        if (ImGui::Selectable(option.c_str(), selected) && !selected)
        {
            mapSize = size;
            edited  = true;
        }
    }
    ImGui::EndCombo();
    return edited;
}

bool drawCascadeSettings(RenderFeature::Shadow::Cascade& cascade, float nearPlane)
{
    bool        edited = drawUnsignedSlider("Shadow cascades", cascade.count, 1, kMaxDirectionalShadowCascades);
    const float minimumShadowDistance = nearPlane * 1.01f;
    edited |= ImGui::DragFloat(
        "Directional shadow distance",
        &cascade.maxDistance,
        0.5f,
        minimumShadowDistance,
        100000.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );

    edited |= drawEnumCombo("Cascade split strategy", cascade.splitStrategy, "Practical (uniform/log)\0Uniform\0");
    if (cascade.splitStrategy == CascadeSplitStrategy::Practical)
        edited |= ImGui::SliderFloat("Cascade split lambda", &cascade.splitLambda, 0.0f, 1.0f, "%.2f");

    edited |= ImGui::SliderFloat("Cascade blend ratio", &cascade.blendRatio, 0.0f, 0.5f, "%.2f");
    ImGui::SetItemTooltip("Blend the last fraction of each cascade into the next. 0 disables blending.");
    edited |= ImGui::DragFloat(
        "Directional PCF radius (texels)",
        &cascade.sampleRadius,
        0.1f,
        0.0f,
        32.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );
    return edited;
}

bool drawShadowSettings(RenderFeature::Shadow& shadow, const vk::PhysicalDeviceLimits& limits, float nearPlane)
{
    bool edited = false;
    if (drawSettingsHeader("Shadows"))
    {
        shadow = {};
        edited = true;
    }
    edited |= ImGui::Checkbox("Enable shadows", &shadow.enable);

    ImGui::BeginDisabled(!shadow.enable);
    edited |= drawEnumCombo("Shadow filter", shadow.filter, "Nearest\0Linear\0");

    edited |= drawShadowMapSize("Point shadow map size", shadow.pointShadowMapSize, limits, true);
    edited |= drawShadowMapSize("Spot shadow map size", shadow.spotShadowMapSize, limits, false);
    edited |= drawShadowMapSize("Rect area shadow map size", shadow.rectAreaShadowMapSize, limits, false);
    edited |= drawShadowMapSize("Directional shadow map size", shadow.cascade.shadowMapSize, limits, false);
    edited |= drawCascadeSettings(shadow.cascade, nearPlane);

    edited |= ImGui::DragFloat("Shadow bias", &shadow.bias, 0.0001f, 0.0f, 1.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
    edited |= ImGui::Checkbox("Per-pixel rotation", &shadow.pixelRotation);
    edited |=
        ImGui::DragFloat("PCF radius", &shadow.sampleRadius, 0.0005f, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
    edited |= ImGui::DragFloat(
        "Spot PCF radius (texels)",
        &shadow.spotSampleRadius,
        0.1f,
        0.0f,
        32.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );
    ImGui::EndDisabled();
    return edited;
}
}  // namespace

bool drawRendererSettings(ConfigManager& config, AssetManager& assets, const VulkanContext& vulkan, SceneAsset& scene)
{
    const auto& current = config.rendererConfig();
    auto        path    = current.renderPath;
    auto        feature = current.feature;

    const bool offline       = scene.renderMode == SceneRenderMode::Offline;
    bool       featureEdited = false;
    if (drawSettingsHeader("Renderer"))
    {
        const RendererConfig defaults;
        if (!offline)
            path = defaults.renderPath;
        feature.ibl                = defaults.feature.ibl;
        feature.ambientCoefficient = defaults.feature.ambientCoefficient;
        featureEdited              = true;
    }
    if (offline)
        ImGui::TextUnformatted("Render path: Offline path tracing");
    else
        path = drawRealtimeRenderPath(path, vulkan.supportsRayTracing());
    ImGui::Spacing();

    featureEdited |= ImGui::Checkbox("IBL", &feature.ibl);
    const bool sceneEdited = feature.ibl && drawEnvironmentSelection(scene, assets);
    featureEdited |= drawPostProcessSettings(feature, scene.editorCamera.exposure.has_value());

    switch (path)
    {
        case RenderPath::OfflinePathTracing:
            featureEdited |= drawOfflineSettings(feature.offline, *context().renderer);
            break;
        case RenderPath::RayTracing:
            featureEdited |= drawRayTracingSettings(feature.rayTracing);
            if (feature.rayTracing.algorithm == RealtimeAlgorithm::ReSTIR)
                featureEdited |= drawRestirSettings(feature.restir);
            break;
        case RenderPath::Forward:
        case RenderPath::Deferred:
            featureEdited |=
                drawShadowSettings(feature.shadow, vulkan.properties().limits, scene.editorCamera.nearPlane);
            break;
    }

    if (path != current.renderPath)
        config.setRenderPath(path);
    if (featureEdited)
        config.setRenderFeature(feature);
    return sceneEdited;
}
