#include "render/RenderContext.h"

namespace
{
RenderContext currentContext;
}

RenderContext& renderContext() { return currentContext; }
