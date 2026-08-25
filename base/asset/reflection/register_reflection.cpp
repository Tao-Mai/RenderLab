#include "base/asset/reflection/register_reflection.h"

#include "base/asset/reflection/component_registry.h"
#include "base/asset/reflection/glm_meta.h"

void reflection::register_all()
{
    component_registry::register_bindings();
}
