#pragma once

#include <cstdint>
#include <vector>

class RenderGraph;

class EditorPickingReadbackPass
{
public:
    enum Input
    {
        Readback,
        InputCount
    };

    void                   registerPass(RenderGraph& graph);
    void                   setupPass(RenderGraph& graph);
    [[nodiscard]] uint32_t readSelectionId(RenderGraph& graph, uint32_t frameIndex) const;

private:
    std::vector<uint32_t> inputSlots;
};
