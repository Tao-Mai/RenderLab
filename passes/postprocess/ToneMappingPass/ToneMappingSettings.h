#pragma once

#include "core/Logger.h"

enum class ToneMappingOperator
{
    Neutral,
    Reinhard,
};

struct ToneMappingSettings
{
    ToneMappingOperator mode = ToneMappingOperator::Neutral;

    void validate() const { CHECK(mode == ToneMappingOperator::Neutral || mode == ToneMappingOperator::Reinhard); }
};
