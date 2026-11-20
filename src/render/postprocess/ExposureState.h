#pragma once

#include <cstdint>

struct ExposureState
{
    float    ev100;
    float    exposureScale;
    uint32_t automaticValid;
    uint32_t padding;
};
static_assert(sizeof(ExposureState) == 16);
