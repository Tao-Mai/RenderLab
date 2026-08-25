#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera
{
public:
    glm::vec3 position{0.0f, 0.5f, 2.0f};
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 world_up{0.0f, 1.0f, 0.0f};
    float     fov = 45.0f;

    bool  orthographic = false;
    float ortho_height = 10.0f;
    float aspect       = 16.0f / 9.0f;

    Camera() = default;

    void look_at(const glm::vec3& eye, const glm::vec3& target,
                 const glm::vec3& up_dir = {0.0f, 1.0f, 0.0f})
    {
        position = eye;
        world_up = up_dir;
        front    = glm::normalize(target - eye);
        up       = world_up;
    }

    [[nodiscard]] glm::mat4 view() const { return glm::lookAt(position, position + front, up); }

    [[nodiscard]] glm::mat4 projection(float near_plane = 0.1f, float far_plane = 1000.0f) const
    {
        if (orthographic)
        {
            const float half_h = ortho_height * 0.5f;
            const float half_w = half_h * aspect;
            return glm::ortho(-half_w, half_w, -half_h, half_h, near_plane, far_plane);
        }
        return glm::perspective(glm::radians(fov), aspect, near_plane, far_plane);
    }

    // 旧名别名，便于渐进迁移
    [[nodiscard]] glm::mat4 get_view_matrix() const { return view(); }
    [[nodiscard]] glm::mat4 get_projection_matrix(float near_plane = 0.1f,
                                                  float far_plane = 1000.0f) const
    {
        return projection(near_plane, far_plane);
    }
};
