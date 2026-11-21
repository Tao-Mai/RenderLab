#pragma once

#include <cstdint>

inline constexpr char kExposurePassType[] = "ExposurePass";
inline constexpr char kExposureStateOutput[] = "state";

struct ExposureState
{
    float    ev100;
    float    exposureScale;
    uint32_t automaticValid;
    uint32_t padding;
};
static_assert(sizeof(ExposureState) == 16);
