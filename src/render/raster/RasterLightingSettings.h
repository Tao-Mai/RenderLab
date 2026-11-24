#pragma once
#include <slang-rhi.h>

#include "core/MetadataAnnotations.h"
#include "render/RenderConfig.h"

struct RL_RECORD RasterLightingSettings
{
    RL_FIELD("ambientCoefficient") float ambientCoefficient = 0.1f;

    void validate() const;
};

struct RL_RECORD RasterLightingOutputSettings : RasterLightingSettings
{
    RL_FIELD("hdr") bool hdr = true;

    [[nodiscard]] rhi::Format getColorFormat(rhi::Format defaultFormat) const
    {
        return hdr ? kHdrColorFormat : defaultFormat;
    }
    void validateColorFormat(rhi::IDevice& device) const;
};

struct RL_RECORD TransparentLightingSettings : RasterLightingSettings
{
    RL_FIELD("clearcoat") bool clearcoat = true;
};

#include RL_METADATA_HEADER("metadata/src/render/raster/RasterLightingSettings.generated.h")
