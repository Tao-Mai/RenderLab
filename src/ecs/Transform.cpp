#include "ecs/Transform.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace ecs
{
glm::vec3 Transform::rotationEulerDegrees() const
{
    const glm::quat value = glm::normalize(rotation);
    const float sinXCosY = 2.0f * (value.w * value.x + value.y * value.z);
    const float cosXCosY = 1.0f - 2.0f * (value.x * value.x + value.y * value.y);
    const float sinY = std::clamp(
        2.0f * (value.w * value.y - value.z * value.x),
        -1.0f,
        1.0f);
    const float sinZCosY = 2.0f * (value.w * value.z + value.x * value.y);
    const float cosZCosY = 1.0f - 2.0f * (value.y * value.y + value.z * value.z);

    return glm::degrees(glm::vec3{
        std::atan2(sinXCosY, cosXCosY),
        std::asin(sinY),
        std::atan2(sinZCosY, cosZCosY),
    });
}

void Transform::setRotationEulerDegrees(const glm::vec3& eulerDegrees)
{
    const glm::vec3 radians = glm::radians(eulerDegrees);
    rotation = glm::normalize(
        glm::angleAxis(radians.z, glm::vec3{0.0f, 0.0f, 1.0f}) *
        glm::angleAxis(radians.y, glm::vec3{0.0f, 1.0f, 0.0f}) *
        glm::angleAxis(radians.x, glm::vec3{1.0f, 0.0f, 0.0f}));
}

glm::mat4 Transform::matrix() const
{
    glm::mat4 result = glm::translate(glm::mat4{1.0f}, position);
    result *= glm::mat4_cast(glm::normalize(rotation));
    return glm::scale(result, scale);
}
}
