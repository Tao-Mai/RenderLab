#pragma once

#include "base/core/reflection/meta_register.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

META_REGISTER(glm::vec2, vec2, x, y);
META_REGISTER(glm::vec3, vec3, x, y, z);
META_REGISTER(glm::vec4, vec4, x, y, z, w);
META_REGISTER(glm::quat, quat, x, y, z, w);
