#include "passes/postprocess/FalcorPostProcess/FalcorPostProcess.h"

#include "editor/PassEditorRegistry.h"
#include "editor/SettingsWidgets.h"

#include <algorithm>
#include <cmath>
#include <glm/matrix.hpp>
#include <limits>

namespace
{
bool drawSettings(FalcorPostProcessConfig& settings, const FalcorPostProcess&, const PassEditorContext&)
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
}

void registerFalcorPostProcessEditor(PassEditorRegistry& editors)
{
    editors.add<FalcorPostProcess, drawSettings>("Falcor Post Process");
}
