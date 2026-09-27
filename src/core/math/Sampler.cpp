//
// Created by 24500 on 2026/9/27.
//

#include "Sampler.h"

#include "core/utils/Bitops.h"

#include <algorithm>
#include <cmath>

constexpr float kUint32ToUnitIntervalScale = 2.3283064365386963e-10f; // 1 / 2^32

float Sampler::VanDerCorput(uint32_t index)
{
    const float sample = static_cast<float>(utils::reverseBit(index)) *
        kUint32ToUnitIntervalScale;
    return std::min(sample, std::nextafter(1.0f, 0.0f));
}

std::vector<std::pair<float, float>> Sampler::HammersleySample2D(int n)
{
    std::vector<std::pair<float, float>> res(n);

    for (int i = 0; i < n; i++)
    {
        res[i] = HammersleySample2D(static_cast<uint32_t>(i), static_cast<uint32_t>(n));
    }

    return res;
}

std::pair<float, float> Sampler::HammersleySample2D(uint32_t index, uint32_t sampleCount)
{
    return {
        static_cast<float>(index) / static_cast<float>(sampleCount),
        VanDerCorput(index),
    };
}
