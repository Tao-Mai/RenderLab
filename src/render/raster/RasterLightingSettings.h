#pragma once

#include "core/MetadataAnnotations.h"
#include "render/resources/Formats.h"

class VulkanContext;

struct RL_RECORD RasterLightingSettings
{
    RL_FIELD("hdr") bool hdr = true;

    [[nodiscard]] Falcor::ResourceFormat getColorFormat(Falcor::ResourceFormat defaultFormat) const
    {
        return hdr ? Falcor::ResourceFormat::RGBA16Float : defaultFormat;
    }
    void validateColorFormat(const VulkanContext& vulkan) const;
};

#include RL_METADATA_HEADER("metadata/src/render/raster/RasterLightingSettings.generated.h")
