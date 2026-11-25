#pragma once

#include "render/impl/postprocess/ExposurePass/ExposurePassSettings.h"
#include "render/impl/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"
#include "render/impl/postprocess/ToneMappingPass/ToneMappingSettings.h"
#include "render/impl/postprocess/bloom/BloomSettings.h"
#include "render/impl/passes/MinimalPathTracerSettings.h"

struct RenderPassOptions
{
    MinimalPathTracerSettings pathTracer;

    ExposurePassSettings exposure;

    BloomSettings bloom;

    ToneMappingSettings toneMapping;

    FalcorPostProcessConfig falcor;

    bool operator==(const RenderPassOptions&) const = default;
};
