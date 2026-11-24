#pragma once
#include <slang-rhi.h>
[[nodiscard]] inline rhi::Extent3D dispatchGroups(rhi::IComputePipeline* pipeline, rhi::Extent3D threads)
{
    SlangUInt groupSize[3];
    pipeline->getProgram()->getSlangComponentType()->getLayout()->getEntryPointByIndex(0)->getComputeThreadGroupSize(
        3,
        groupSize
    );
    return {
        uint32_t((threads.width + groupSize[0] - 1) / groupSize[0]),
        uint32_t((threads.height + groupSize[1] - 1) / groupSize[1]),
        uint32_t((threads.depth + groupSize[2] - 1) / groupSize[2])
    };
}
