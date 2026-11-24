#include "core/Object.h"

#include "core/Logger.h"

namespace Falcor
{
void Object::incRef() const { mRefCount.fetch_add(1, std::memory_order_relaxed); }

void Object::decRef(bool dealloc) const noexcept
{
    const auto previous = mRefCount.fetch_sub(1, std::memory_order_acq_rel);

    if (previous == 1 && dealloc)
    {
        // The virtual destructor and its deallocation function may both reside in the plugin.
        const auto library = mPluginLibrary;
        delete this;
    }
}
}  // namespace Falcor
