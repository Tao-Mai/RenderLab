#pragma once

#include "core/Macros.h"
#include "core/math/Vector.h"
#include "render/CopyContext.h"

namespace Falcor
{
class UnorderedAccessView;

class FALCOR_API ComputeContext : public CopyContext
{
public:
    using CopyContext::CopyContext;

    void clearUAV(const UnorderedAccessView* view, const float4& value);
    void clearUAV(const UnorderedAccessView* view, const uint4& value);
};
}  // namespace Falcor
