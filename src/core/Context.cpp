#include "core/Context.h"

namespace
{
Context g_context;
}

Context& context()
{
    return g_context;
}
