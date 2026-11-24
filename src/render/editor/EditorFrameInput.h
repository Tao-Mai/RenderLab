#pragma once

#include <slang-rhi.h>
#include <cstdint>
#include <functional>


inline constexpr char kPickingImageOutput[] = "pickingImage";

struct EditorFrameInput
{
    bool                                                    requestPick = false;
    uint32_t                                                pickX       = 0;
    uint32_t                                                pickY       = 0;
    std::function<void(rhi::IRenderPassEncoder&, uint32_t)> recordUi;
};
