#include "core/context.h"

namespace
{
Context g_context;
}

Context& context()
{
    return g_context;
}
