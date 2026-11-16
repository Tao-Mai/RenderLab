#include "editor/RendererSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/matrix.hpp>
#include <imgui.h>
#include <limits>
#include <numbers>
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

}  // namespace
bool drawRendererSettings(ConfigManager& config, AssetManager& assets, const VulkanContext& vulkan, SceneAsset& scene)
{
    bool          sceneEdited = false;
    RenderFeature feature     = config.rendererConfig().feature;
    if (drawSettingsHeader("Renderer"))
    {
        const RendererConfig defaults;
        if (scene.renderMode == SceneRenderMode::Realtime)
            config.setRenderPath(defaults.renderPath);
        feature.ibl                = defaults.feature.ibl;
        feature.ambientCoefficient = defaults.feature.ambientCoefficient;
        config.setRenderFeature(feature);
    }
    const RenderPath     path = config.rendererConfig().renderPath;
    constexpr std::array kRenderPaths{
        std::pair{RenderPath::Forward, "Forward"},
        std::pair{RenderPath::Deferred, "Deferred"},
        std::pair{RenderPath::RayTracing, "Real-time ray tracing"},
    };
    if (scene.renderMode == SceneRenderMode::Offline)
        ImGui::TextUnformatted("Render path: Offline path tracing");
    else
    {
        const auto selected =
            std::ranges::find_if(kRenderPaths, [path](const auto& option) { return option.first == path; });
        DCHECK(selected != kRenderPaths.end());
        if (ImGui::BeginCombo("Render path", selected->second))
        {
            const bool supported = vulkan.supportsRayTracing();
            for (const auto& [candidate, label] : kRenderPaths)
            {
                ImGui::BeginDisabled(candidate == RenderPath::RayTracing && !supported);
                if (ImGui::Selectable(label, path == candidate) && path != candidate)
                    config.setRenderPath(candidate);
                ImGui::EndDisabled();
            }
            if (!supported)
                ImGui::TextUnformatted("Ray tracing is not supported by this GPU");
            ImGui::EndCombo();
        }
    }
    ImGui::Spacing();

    if (ImGui::Checkbox("IBL", &feature.ibl))
    {
        config.setRenderFeature(feature);
    }

    if (feature.ibl)
    {
        sceneEdited = drawEnvironmentSelection(scene, assets);
    }

    if (ImGui::CollapsingHeader("Post Process"))
    {
        int postProcessMode = static_cast<int>(feature.postProcess);
        if (ImGui::Combo("Post process", &postProcessMode, "RenderLab\0Falcor\0"))
        {
            feature.postProcess = static_cast<PostProcessMode>(postProcessMode);
            config.setRenderFeature(feature);
        }
        if (drawSettingsHeader("HDR"))
        {
            feature.hdr = RenderFeature{}.hdr;
            if (feature.postProcess == PostProcessMode::RenderLab)
                feature.toneMapping = {};
            else
                feature.falcorPostProcess.toneMapper = {};
            config.setRenderFeature(feature);
        }
        if (ImGui::Checkbox("Enable HDR", &feature.hdr))
        {
            config.setRenderFeature(feature);
        }
        if (feature.postProcess == PostProcessMode::Falcor)
        {
            ImGui::BeginDisabled(!feature.hdr);
            if (drawFalcorPostProcessSettings(feature.falcorPostProcess))
                config.setRenderFeature(feature);
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::BeginDisabled(!feature.hdr);
            int toneMappingMode = static_cast<int>(feature.toneMapping.mode);
            if (ImGui::Combo("Operator", &toneMappingMode, "Khronos PBR Neutral\0Reinhard\0"))
            {
                feature.toneMapping.mode = static_cast<ToneMappingOperator>(toneMappingMode);
                config.setRenderFeature(feature);
            }

            ImGui::TextUnformatted("Global exposure");
            ImGui::PushID("GlobalExposure");
            if (drawExposureSettings(feature.toneMapping.exposure))
            {
                config.setRenderFeature(feature);
            }
            ImGui::PopID();

            if (scene.editorCamera.exposure)
            {
                ImGui::TextDisabled("Exposure overridden by Editor camera");
            }

            ImGui::EndDisabled();

            if (drawSettingsHeader("Bloom"))
            {
                feature.bloom = {};
                config.setRenderFeature(feature);
            }
            ImGui::BeginDisabled(!feature.hdr);
            if (ImGui::Checkbox("Enable bloom", &feature.bloom.enable))
                config.setRenderFeature(feature);
            ImGui::SetItemTooltip(
                "HDR scattering bloom; light markers are included, editor UI is excluded. Requires HDR."
            );
            ImGui::BeginDisabled(!feature.bloom.enable);
            bool bloomEdited = ImGui::SliderFloat("Bloom intensity", &feature.bloom.intensity, 0.0f, 1.0f, "%.3f");
            ImGui::SetItemTooltip("Fraction of scene light redistributed by bloom, not additive brightness.");
            bloomEdited =
                ImGui::SliderFloat("Bloom scatter", &feature.bloom.scatter, 0.05f, 0.95f, "%.2f") || bloomEdited;
            ImGui::SetItemTooltip("Higher values give coarser pyramid levels more weight and produce a wider glow.");
            bloomEdited = ImGui::DragFloat(
                              "Gaussian radius",
                              &feature.bloom.filterRadius,
                              0.05f,
                              0.1f,
                              5.0f,
                              "%.2f",
                              ImGuiSliderFlags_AlwaysClamp
                          ) ||
                bloomEdited;
            int bloomLevels = static_cast<int>(feature.bloom.levels);
            if (ImGui::SliderInt("Bloom levels", &bloomLevels, 1, static_cast<int>(kMaxBloomLevels)))
            {
                feature.bloom.levels = static_cast<uint32_t>(bloomLevels);
                bloomEdited          = true;
            }
            if (bloomEdited)
                config.setRenderFeature(feature);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        }
    }

    if (config.rendererConfig().renderPath == RenderPath::OfflinePathTracing)
    {
        if (drawSettingsHeader("Offline rendering"))
        {
            feature.offline = {};
            config.setRenderFeature(feature);
        }

        ImGui::Text("Samples per pixel: %.2f", context().renderer->offlineSampleProgress());
        if (context().renderer->offlinePreparingPipeline())
            ImGui::TextUnformatted("Preparing renderer...");

        int algorithm = static_cast<int>(feature.offline.algorithm);
        if (ImGui::Combo("Algorithm", &algorithm, "PT\0BDPT\0"))
        {
            feature.offline.algorithm = static_cast<OfflineAlgorithm>(algorithm);
            config.setRenderFeature(feature);
        }

        auto& bounceSetting = feature.offline.algorithm == OfflineAlgorithm::PT ? feature.offline.pt.maxBounces
                                                                                : feature.offline.bdpt.maxBounces;
        int   maxBounces    = static_cast<int>(bounceSetting);
        if (ImGui::SliderInt(
                "Max bounces",
                &maxBounces,
                static_cast<int>(kMinPathBounces),
                static_cast<int>(kMaxPathBounces),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            bounceSetting = static_cast<uint32_t>(maxBounces);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");

        if (feature.offline.algorithm == OfflineAlgorithm::BDPT)
        {
            auto& bdpt   = feature.offline.bdpt;
            bool  edited = ImGui::Checkbox("Light tracing", &bdpt.lightTracing);
            ImGui::SetItemTooltip("Connect light subpaths to the camera, including specular caustics.");

            int selection = static_cast<int>(bdpt.lightSelection);
            if (ImGui::Combo("Light selection", &selection, "Uniform\0Power\0"))
            {
                bdpt.lightSelection = static_cast<BdptLightSelection>(selection);
                edited              = true;
            }

            int heuristic = static_cast<int>(bdpt.misHeuristic);
            if (ImGui::Combo("MIS heuristic", &heuristic, "Balance\0Power (2)\0"))
            {
                bdpt.misHeuristic = static_cast<BdptMisHeuristic>(heuristic);
                edited            = true;
            }

            if (edited)
                config.setRenderFeature(feature);
        }

        ImGui::TextUnformatted("Scene, camera or sampling changes reset accumulation.");

        return sceneEdited;
    }

    if (config.rendererConfig().renderPath == RenderPath::RayTracing)
    {
        if (drawSettingsHeader("Ray tracing"))
        {
            feature.rayTracing = {};
            config.setRenderFeature(feature);
        }
        int algorithm = static_cast<int>(feature.rayTracing.algorithm);
        if (ImGui::Combo("Tracing algorithm", &algorithm, "ReSTIR\0Simple\0"))
        {
            feature.rayTracing.algorithm = static_cast<RealtimeAlgorithm>(algorithm);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip(
            "Simple: ordinary light sampling and BRDF bounces. ReSTIR: reservoir-based direct lighting and reuse."
        );
        int maxBounces = static_cast<int>(feature.rayTracing.maxBounces);
        if (ImGui::SliderInt(
                "Max bounces",
                &maxBounces,
                static_cast<int>(kMinPathBounces),
                static_cast<int>(kMaxPathBounces),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.rayTracing.maxBounces = static_cast<uint32_t>(maxBounces);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");

        if (ImGui::Checkbox("Enable SVGF", &feature.rayTracing.svgf))
            config.setRenderFeature(feature);
        ImGui::SetItemTooltip(
            "Temporally accumulates direct and indirect lighting, estimates variance and applies\n"
            "five edge-aware wavelet levels. The final level combines the result with emission."
        );

        ImGui::BeginDisabled(!feature.rayTracing.svgf);
        int historyLength = static_cast<int>(feature.rayTracing.historyLength);
        if (ImGui::SliderInt(
                "History length",
                &historyLength,
                1,
                static_cast<int>(kMaxSvgfHistoryLength),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.rayTracing.historyLength = static_cast<uint32_t>(historyLength);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip(
            "Average the first N samples arithmetically; use EMA when the sample count exceeds N.\n"
            "Applies to color and both luminance moments for direct and indirect lighting."
        );
        if (ImGui::SliderFloat(
                "EMA sample weight",
                &feature.rayTracing.emaAlpha,
                0.0f,
                1.0f,
                "%.3f",
                ImGuiSliderFlags_AlwaysClamp
            ))
            config.setRenderFeature(feature);
        ImGui::SetItemTooltip(
            "After history length: new mean = history * (1 - weight) + current sample * weight.\n"
            "The first sample, or a sample with rejected history, has weight 1."
        );
        ImGui::EndDisabled();

        if (feature.rayTracing.algorithm == RealtimeAlgorithm::Simple)
            return sceneEdited;

        if (drawSettingsHeader("ReSTIR"))
        {
            feature.restir = {};
            config.setRenderFeature(feature);
        }
        int candidateCount = static_cast<int>(feature.restir.candidateCount);
        if (ImGui::SliderInt(
                "Light candidates (M)",
                &candidateCount,
                1,
                static_cast<int>(kMaxRestirCandidateCount),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.restir.candidateCount = static_cast<uint32_t>(candidateCount);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip(
            "Light candidates plus one BRDF candidate per surface. Primary direct lighting uses ReSTIR DI."
        );
        bool restirEdited = ImGui::Checkbox("Temporal reuse", &feature.restir.temporalReuse);
        restirEdited      = ImGui::Checkbox("Spatial reuse", &feature.restir.spatialReuse) || restirEdited;

        int historyLimit = static_cast<int>(feature.restir.maxHistoryLength);
        if (ImGui::SliderInt(
                "Reservoir history length",
                &historyLimit,
                1,
                static_cast<int>(kMaxRestirHistoryLength),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.restir.maxHistoryLength = static_cast<uint32_t>(historyLimit);
            restirEdited                    = true;
        }
        int spatialSamples = static_cast<int>(feature.restir.spatialSampleCount);
        if (ImGui::SliderInt(
                "Spatial neighbors",
                &spatialSamples,
                1,
                static_cast<int>(kMaxRestirSpatialSamples),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.restir.spatialSampleCount = static_cast<uint32_t>(spatialSamples);
            restirEdited                      = true;
        }
        restirEdited = ImGui::SliderFloat(
                           "Spatial radius",
                           &feature.restir.spatialRadius,
                           1.0f,
                           kMaxRestirSpatialRadius,
                           "%.1f px",
                           ImGuiSliderFlags_AlwaysClamp
                       ) ||
            restirEdited;
        if (restirEdited)
        {
            config.setRenderFeature(feature);
        }
        return sceneEdited;
    }

    if (drawSettingsHeader("Shadows"))
    {
        feature.shadow = {};
        config.setRenderFeature(feature);
    }
    if (ImGui::Checkbox("Enable shadows", &feature.shadow.enable))
    {
        config.setRenderFeature(feature);
    }

    ImGui::BeginDisabled(!feature.shadow.enable);
    int shadowFilter = static_cast<int>(feature.shadow.filter);
    if (ImGui::Combo("Shadow filter", &shadowFilter, "Nearest\0Linear\0"))
    {
        feature.shadow.filter = static_cast<SamplerAsset::Filter>(shadowFilter);
        config.setRenderFeature(feature);
    }

    const auto drawShadowMapSize = [&](const char* label, uint32_t& mapSize, bool cube)
    {
        const std::string shadowSize = std::to_string(mapSize);
        if (!ImGui::BeginCombo(label, shadowSize.c_str()))
            return;

        constexpr uint32_t kShadowMapSizes[] = {256, 512, 1024, 2048, 4096, 8192};
        const auto&        limits            = vulkan.properties().limits;
        for (uint32_t size : kShadowMapSizes)
        {
            if ((cube && size > limits.maxImageDimensionCube) || size > limits.maxImageDimension2D ||
                size > limits.maxFramebufferWidth || size > limits.maxFramebufferHeight)
                continue;

            const bool        selected = mapSize == size;
            const std::string label    = std::to_string(size);
            if (ImGui::Selectable(label.c_str(), selected) && !selected)
            {
                mapSize = size;
                config.setRenderFeature(feature);
            }
        }
        ImGui::EndCombo();
    };
    drawShadowMapSize("Point shadow map size", feature.shadow.pointShadowMapSize, true);
    drawShadowMapSize("Spot shadow map size", feature.shadow.spotShadowMapSize, false);
    drawShadowMapSize("Rect area shadow map size", feature.shadow.rectAreaShadowMapSize, false);
    drawShadowMapSize("Directional shadow map size", feature.shadow.cascade.shadowMapSize, false);
    int cascadeCount = static_cast<int>(feature.shadow.cascade.count);
    if (ImGui::SliderInt("Shadow cascades", &cascadeCount, 1, static_cast<int>(kMaxDirectionalShadowCascades)))
    {
        feature.shadow.cascade.count = static_cast<uint32_t>(cascadeCount);
        config.setRenderFeature(feature);
    }
    const float minimumShadowDistance = scene.editorCamera.nearPlane * 1.01f;
    if (ImGui::DragFloat(
            "Directional shadow distance",
            &feature.shadow.cascade.maxDistance,
            0.5f,
            minimumShadowDistance,
            100000.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        config.setRenderFeature(feature);
    int splitStrategy = static_cast<int>(feature.shadow.cascade.splitStrategy);
    if (ImGui::Combo("Cascade split strategy", &splitStrategy, "Practical (uniform/log)\0Uniform\0"))
    {
        feature.shadow.cascade.splitStrategy = static_cast<CascadeSplitStrategy>(splitStrategy);
        config.setRenderFeature(feature);
    }
    if (feature.shadow.cascade.splitStrategy == CascadeSplitStrategy::Practical)
        if (ImGui::SliderFloat("Cascade split lambda", &feature.shadow.cascade.splitLambda, 0.0f, 1.0f, "%.2f"))
            config.setRenderFeature(feature);
    if (ImGui::SliderFloat("Cascade blend ratio", &feature.shadow.cascade.blendRatio, 0.0f, 0.5f, "%.2f"))
        config.setRenderFeature(feature);
    ImGui::SetItemTooltip("Blend the last fraction of each cascade into the next. 0 disables blending.");
    if (ImGui::DragFloat(
            "Directional PCF radius (texels)",
            &feature.shadow.cascade.sampleRadius,
            0.1f,
            0.0f,
            32.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        config.setRenderFeature(feature);
    if (ImGui::DragFloat(
            "Shadow bias",
            &feature.shadow.bias,
            0.0001f,
            0.0f,
            1.0f,
            "%.5f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        config.setRenderFeature(feature);
    }
    if (ImGui::Checkbox("Per-pixel rotation", &feature.shadow.pixelRotation))
    {
        config.setRenderFeature(feature);
    }
    if (ImGui::DragFloat(
            "PCF radius",
            &feature.shadow.sampleRadius,
            0.0005f,
            0.0f,
            1.0f,
            "%.4f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        config.setRenderFeature(feature);
    }
    if (ImGui::DragFloat(
            "Spot PCF radius (texels)",
            &feature.shadow.spotSampleRadius,
            0.1f,
            0.0f,
            32.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        config.setRenderFeature(feature);
    ImGui::EndDisabled();
    return sceneEdited;
}
