#pragma once

#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>


struct Transform
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale    = glm::vec3(1.0f);

    [[nodiscard]] glm::mat4 to_model() const
    {
        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) *
               glm::scale(glm::mat4(1.0f), scale);
    }
};

#include "base/core/reflection/meta_register.h"

META_REGISTER(Transform, Transform, position, rotation, scale);
