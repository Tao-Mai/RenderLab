#pragma once

#include <cstdint>
#include <vector>

class RenderGraph;

class RayTracingBlitPass
{
public:
    enum Input
    {
        RayImage,
        Color,
        InputCount
    };
    enum Output
    {
        ColorResult,
        OutputCount
    };

    void registerPass(RenderGraph& graph);
    void setupPass(RenderGraph& graph);
    void executePass(RenderGraph& graph) const;

private:
    std::vector<uint32_t> inputSlots;
    std::vector<uint32_t> outputSlots;
};
