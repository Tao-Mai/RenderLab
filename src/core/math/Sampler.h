//
// Created by 24500 on 2026/9/27.
//

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

class Sampler
{
public:
    static float UniformSample();

    // 以 2 为底的 Van der Corput 序列，返回第 index 个 [0, 1) 样本。
    [[nodiscard]] static float VanDerCorput(uint32_t index);

    // 返回总数为 sampleCount 时第 index 个二维样本，要求 sampleCount > 0 且 index < sampleCount。
    [[nodiscard]] static std::pair<float, float> HammersleySample2D(uint32_t index, uint32_t sampleCount);

    // 一次性生成 n 个 [0, 1) 内的二维样本。
    static std::vector<std::pair<float, float>> HammersleySample2D(int n);
};
