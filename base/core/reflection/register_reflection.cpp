#include "base/core/reflection/register_reflection.h"

#include "base/core/reflection/component_registry.h"
#include "base/core/reflection/glm_meta.h"

void reflection::register_all()
{
    component_registry::register_bindings();
}
