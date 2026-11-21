#include "passes/postprocess/FalcorPostProcess/FalcorPostProcess.h"

#include "editor/PassEditor.h"

#include <algorithm>
#include <cmath>
#include <glm/matrix.hpp>
#include <limits>

namespace
{
bool drawSettings(Falcor::Gui::Widgets& widgets, FalcorPostProcessConfig& settings, const FalcorPostProcess&,
                  const PassEditorContext&)
{
    using namespace falcorPostProcess;
    bool  edited     = false;
    auto& toneMapper = settings.toneMapper;
    if (widgets.resetHeader("Falcor ToneMapper"))
    {
        toneMapper = {};
        edited     = true;
    }
    widgets.disabledText("Falcor exposure is independent of camera exposure.");
    edited |= widgets.slider(
        "Exposure Compensation",
        toneMapper.exposureCompensation,
        kExposureCompensationMin,
        kExposureCompensationMax,
        "%.1f"
    );
    edited |= widgets.checkbox("Auto Exposure", toneMapper.autoExposure);
    if (!toneMapper.autoExposure)
    {
        edited |= widgets.dropdown("Exposure mode", toneMapper.exposureMode, {"Aperture priority", "Shutter priority"});
        float       ev    = toneMapper.exposureValue();
        const float minEv = std::log2(kShutterMin * kFNumberMin * kFNumberMin);
        const float maxEv = std::log2(kShutterMax * kFNumberMax * kFNumberMax);
        if (widgets.slider("Exposure Value (EV)", ev, minEv, maxEv, "%.1f"))
        {
            toneMapper.setExposureValue(ev);
            edited = true;
        }
        edited |= widgets.drag("Film Speed (ISO)", toneMapper.filmSpeed, 1.0f, kFilmSpeedMin, kFilmSpeedMax, "%.1f");
        edited |= widgets.drag("f-Number", toneMapper.fNumber, 0.1f, kFNumberMin, kFNumberMax, "%.1f");
        edited |= widgets.drag("Shutter", toneMapper.shutter, 0.1f, kShutterMin, kShutterMax, "%.1f");
        widgets.tooltip("Reciprocal shutter time (1/seconds), matching Falcor.");
    }
    edited |= widgets.checkbox("White Balance", toneMapper.whiteBalance);
    if (toneMapper.whiteBalance)
    {
        edited |= widgets.drag("White Point (K)", toneMapper.whitePoint, 5.0f, kWhitePointMin, kWhitePointMax, "%.0f");
        auto white = glm::inverse(toneMapper.whiteBalanceTransform()) * glm::vec3{1.0f};
        white /= std::max({white.r, white.g, white.b});
        widgets.colorPreview("Source white", white);
    }
    edited |= widgets.dropdown("Operator", toneMapper.mode,
                               {"Linear", "Reinhard", "Reinhard Modified", "Heji Hable ALU", "Hable UC2", "ACES"});
    if (toneMapper.mode == FalcorToneMappingOperator::ReinhardModified)
        edited |= widgets.drag(
            "White Luminance",
            toneMapper.whiteMaxLuminance,
            0.2f,
            0.1f,
            std::numeric_limits<float>::max(),
            "%.2f"
        );
    if (toneMapper.mode == FalcorToneMappingOperator::HableUc2)
        edited |= widgets.drag("Linear White", toneMapper.whiteScale, 0.01f, 0.001f, 100.0f, "%.2f");
    edited |= widgets.checkbox("Clamp Output", toneMapper.clampOutput);

    auto& postFx = settings.simplePostFx;
    widgets.pushId("FalcorSimplePostFX");
    if (widgets.resetHeader("Falcor SimplePostFX"))
    {
        postFx = {};
        edited = true;
    }
    edited |= widgets.checkbox("Enable post fx", postFx.enabled);
    edited |= widgets.slider("Wipe", postFx.wipe, 0.0f, 1.0f);
    if (auto group = widgets.group("Lens FX", true))
    {
        edited |= widgets.slider("Bloom", postFx.bloomAmount, 0.0f, 1.0f);
        edited |= widgets.slider("Bloom Star", postFx.starAmount, 0.0f, 1.0f);
        edited |= widgets.slider("Star Angle", postFx.starAngle, 0.0f, 1.0f);
        edited |= widgets.slider("Vignette", postFx.vignetteAmount, 0.0f, 1.0f);
        edited |= widgets.slider("Chromatic Aberration", postFx.chromaticAberrationAmount, 0.0f, 1.0f);
        edited |= widgets.slider("Barrel Distortion", postFx.barrelDistortAmount, 0.0f, 1.0f);
        if (widgets.button("Reset lens FX"))
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
    }
    if (auto group = widgets.group("Saturation"))
    {
        edited |= widgets.slider("Shadow Saturation", postFx.saturationCurve.x, 0.0f, 2.0f);
        edited |= widgets.slider("Midtone Saturation", postFx.saturationCurve.y, 0.0f, 2.0f);
        edited |= widgets.slider("Highlight Saturation", postFx.saturationCurve.z, 0.0f, 2.0f);
        if (widgets.button("Reset saturation"))
        {
            postFx.saturationCurve = FalcorPostFxConfig{}.saturationCurve;
            edited                 = true;
        }
    }
    if (auto group = widgets.group("Offset/Power/Scale (luma)"))
    {
        edited |= widgets.slider("Luma Offset (Shadows)", postFx.colorOffsetScalar, -1.0f, 1.0f);
        edited |= widgets.slider("Luma Power (Midtones)", postFx.colorPowerScalar, -1.0f, 1.0f);
        edited |= widgets.slider("Luma Scale (Highlights)", postFx.colorScaleScalar, -1.0f, 1.0f);
        if (widgets.button("Reset luma"))
        {
            postFx.colorOffsetScalar = 0.0f;
            postFx.colorScaleScalar  = 0.0f;
            postFx.colorPowerScalar  = 0.0f;
            edited                   = true;
        }
    }
    if (auto group = widgets.group("Offset/Power/Scale (color)"))
    {
        edited |= widgets.colorEdit("Color Offset (Shadows)", postFx.colorOffset);
        edited |= widgets.colorEdit("Color Power (Midtones)", postFx.colorPower);
        edited |= widgets.colorEdit("Color Scale (Highlights)", postFx.colorScale);
        if (widgets.button("Reset color"))
        {
            const FalcorPostFxConfig defaults;
            postFx.colorOffset = defaults.colorOffset;
            postFx.colorScale  = defaults.colorScale;
            postFx.colorPower  = defaults.colorPower;
            edited             = true;
        }
    }
    widgets.popId();
    return edited;
}
}

void FalcorPostProcess::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context->passEditor, "Falcor Post Process");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, FalcorPostProcess>();
}
