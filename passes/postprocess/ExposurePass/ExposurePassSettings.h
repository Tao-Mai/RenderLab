#pragma once

#include "core/MetadataAnnotations.h"

#include "core/Logger.h"
#include "render/ExposureConfig.h"

struct RL_RECORD ExposurePassSettings : ExposureConfig
{
    void validate() const { CHECK(isValid()); }
};

#include RL_METADATA_HEADER("metadata/passes/postprocess/ExposurePass/ExposurePassSettings.generated.h")
