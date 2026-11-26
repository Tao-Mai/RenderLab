#pragma once

#include "render/impl/postprocess/ExposurePass/ExposurePassSettings.h"
#include "render/impl/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"
#include "render/impl/postprocess/ToneMappingPass/ToneMappingSettings.h"
#include "render/impl/postprocess/bloom/BloomSettings.h"
#include "render/impl/passes/minimal/MinimalPathTracerSettings.h"
#include "render/impl/passes/restir/RestirPathTracerSettings.h"

struct RenderPassOptions
{
    MinimalPathTracerSettings pathTracer;

    RestirPathTracerSettings restir;

    ExposurePassSettings exposure;

    BloomSettings bloom;

    ToneMappingSettings toneMapping;

    FalcorPostProcessConfig falcor;

    bool showAnalyticLights = false;

    bool operator==(const RenderPassOptions&) const = default;
};
