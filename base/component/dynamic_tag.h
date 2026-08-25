#pragma once

struct DynamicTag
{
    bool value = false;
};

#include "base/asset/reflection/meta_register.h"

META_REGISTER(DynamicTag, DynamicTag, value);
