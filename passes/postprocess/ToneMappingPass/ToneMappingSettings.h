#pragma once

#include "core/MetadataAnnotations.h"

#include "core/Logger.h"

enum class RL_ENUM ToneMappingOperator
{
    Neutral RL_ENUM_VALUE("Neutral"),
    Reinhard RL_ENUM_VALUE("Reinhard"),
};

struct RL_RECORD ToneMappingSettings
{
    RL_FIELD("mode") ToneMappingOperator mode = ToneMappingOperator::Neutral;

    void validate() const { CHECK(mode == ToneMappingOperator::Neutral || mode == ToneMappingOperator::Reinhard); }
};

#include RL_METADATA_HEADER("metadata/passes/postprocess/ToneMappingPass/ToneMappingSettings.generated.h")
