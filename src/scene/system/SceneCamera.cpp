#include "scene/system/SceneCamera.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

glm::vec3 SceneCamera::forward() const
{
    const float yawRadians   = glm::radians(yaw);
    const float pitchRadians = glm::radians(pitch);
    return glm::normalize(
        glm::vec3{
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians),
            std::sin(yawRadians) * std::cos(pitchRadians),
        }
    );
}

glm::vec3 SceneCamera::right() const
{
    const float yawRadians = glm::radians(yaw);
    return {-std::sin(yawRadians), 0.0f, std::cos(yawRadians)};
}

glm::vec3 SceneCamera::up() const { return glm::normalize(glm::cross(right(), forward())); }

glm::mat4 SceneCamera::viewMatrix() const { return glm::lookAt(position, position + forward(), up()); }

glm::mat4 SceneCamera::projectionMatrix(float aspectRatio) const
{
    return glm::perspective(glm::radians(fieldOfView), aspectRatio, nearPlane, farPlane);
}
