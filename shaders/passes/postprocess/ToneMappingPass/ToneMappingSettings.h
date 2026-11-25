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
    RL_FIELD("mode") RL_PASS_UI() ToneMappingOperator mode = ToneMappingOperator::Neutral;

    void validate() const { CHECK(mode == ToneMappingOperator::Neutral || mode == ToneMappingOperator::Reinhard); }
};
