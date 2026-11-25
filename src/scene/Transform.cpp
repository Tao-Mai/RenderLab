#define GLM_ENABLE_EXPERIMENTAL
#include "scene/Transform.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace
{
constexpr float kShearTolerance = 1e-6f;
}

bool Transform::setMatrix(const glm::mat4& matrix)
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

glm::mat4 Transform::trsMatrix() const
{
    glm::mat4 result = glm::translate(glm::mat4{1.0f}, position);
    result *= glm::mat4_cast(glm::normalize(rotation));
    
    return glm::scale(result, scale);
}

glm::mat4 Transform::matrix() const { return trsMatrix() * base; }
