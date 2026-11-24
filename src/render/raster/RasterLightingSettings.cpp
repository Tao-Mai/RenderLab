#include "render/raster/RasterLightingSettings.h"

#include <slang-rhi.h>
#include "core/Logger.h"
#include "render/RenderConfig.h"
#include "render/device/RhiCheck.h"

void RasterLightingSettings::validateColorFormat(rhi::IDevice& device) const
{
    if (!hdr)
        return;
    rhi::FormatSupport support;
    rhiCheck(device.getFormatSupport(kHdrColorFormat, &support));
    const auto kRequired =
        rhi::FormatSupport::RenderTarget | rhi::FormatSupport::Blendable | rhi::FormatSupport::ShaderSample;
    CHECK((support & kRequired) == kRequired);
}
