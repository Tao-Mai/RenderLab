#define GLM_ENABLE_EXPERIMENTAL
#include "scene/component/TransformComponent.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace
{
constexpr float kShearTolerance = 1e-6f;
}

glm::vec3 TransformComponent::rotationEulerDegrees() const
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

void TransformComponent::setRotationEulerDegrees(const glm::vec3& eulerDegrees)
{
    const glm::vec3 radians = glm::radians(eulerDegrees);
    rotation = glm::normalize(
        glm::angleAxis(radians.z, glm::vec3{0.0f, 0.0f, 1.0f}) *
        glm::angleAxis(radians.y, glm::vec3{0.0f, 1.0f, 0.0f}) *
        glm::angleAxis(radians.x, glm::vec3{1.0f, 0.0f, 0.0f}));
}

bool TransformComponent::setMatrix(const glm::mat4& matrix)
{
    for (glm::length_t column = 0; column < 4; ++column)
        for (glm::length_t row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]))
                return false;
    if (matrix[0][3] != 0.0f || matrix[1][3] != 0.0f || matrix[2][3] != 0.0f || matrix[3][3] != 1.0f)
        return false;

    glm::dvec3 importedScale, importedPosition, skew;
    glm::dquat importedRotation;
    glm::dvec4 perspective;
    if (!glm::decompose(glm::dmat4(matrix), importedScale, importedRotation, importedPosition, skew, perspective))
        return false;
    for (glm::length_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(static_cast<float>(importedScale[axis])) || !std::isfinite(skew[axis]))
            return false;
    for (glm::length_t axis = 0; axis < 4; ++axis)
        if (!std::isfinite(importedRotation[axis]))
            return false;

    position = glm::vec3(matrix[3]);
    if (std::abs(skew.x) > kShearTolerance || std::abs(skew.y) > kShearTolerance ||
        std::abs(skew.z) > kShearTolerance)
    {
        base = glm::mat4(glm::mat3(matrix));
        rotation = glm::quat{1, 0, 0, 0};
        scale = glm::vec3{1};
    }
    else
    {
        base = glm::mat4{1};
        rotation = glm::normalize(glm::quat(importedRotation));
        scale = glm::vec3(importedScale);
    }
    return true;
}

glm::mat4 TransformComponent::trsMatrix() const
{
    glm::mat4 result = glm::translate(glm::mat4{1.0f}, position);
    result *= glm::mat4_cast(glm::normalize(rotation));
    return glm::scale(result, scale);
}

glm::mat4 TransformComponent::matrix() const { return trsMatrix() * base; }
