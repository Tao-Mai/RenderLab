#pragma once

struct DynamicTag
{
    bool value = false;
};

#include "base/core/reflection/meta_register.h"

META_REGISTER(DynamicTag, DynamicTag, value);
