#pragma once

#include <cstdint>
#include <string>

namespace path_tracing_test
{
struct OfflineProgress
{
    uint32_t sampleCount;
    float sampleProgress;
    bool preparingPipeline;
};

[[nodiscard]] inline std::string statsKey(const std::string& passName)
{
    return "test.pathTracingStats." + passName;
}
}
