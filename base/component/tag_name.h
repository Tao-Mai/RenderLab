#pragma once

#include <string>

struct TagName
{
    std::string value;
};

#include "base/core/reflection/meta_register.h"

META_REGISTER(TagName, TagName, value);
