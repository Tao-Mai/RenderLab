#pragma once

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace asset_json
{
inline glm::vec3 read_vec3(const nlohmann::json& j, const glm::vec3& fallback = {})
{
    if (!j.is_array() || j.size() < 3)
        return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

inline nlohmann::json write_vec3(const glm::vec3& v)
{
    return nlohmann::json::array({v.x, v.y, v.z});
}

inline glm::quat read_quat_euler_deg(const nlohmann::json& j, const glm::quat& fallback = {})
{
    if (!j.is_array() || j.size() < 3)
        return fallback;
    const glm::vec3 euler_deg = read_vec3(j);
    return glm::quat(glm::radians(euler_deg));
}

inline nlohmann::json write_quat_euler_deg(const glm::quat& q)
{
    return write_vec3(glm::degrees(glm::eulerAngles(q)));
}
}  // namespace asset_json
