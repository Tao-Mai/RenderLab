#pragma once

#include <cstdint>
#include <vector>

class RenderGraph;

class EditorPickingCopyPass
{
public:
    enum Input
    {
        PickingImage,
        Readback,
        InputCount
    };
    enum Output
    {
        ReadbackResult,
        OutputCount
    };

    void registerPass(RenderGraph& graph);
    void setupPass(RenderGraph& graph);
    void executePass(RenderGraph& graph) const;

private:
    std::vector<uint32_t> inputSlots;
    std::vector<uint32_t> outputSlots;
};
