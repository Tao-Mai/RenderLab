#include "base/core/reflection/meta_json.h"

#include "base/asset/json_util.h"

#include <entt/core/hashed_string.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <glog/logging.h>
#include <nlohmann/json.hpp>

#include <string>

namespace
{
using json = nlohmann::json;

bool is_glm_vec3(const entt::meta_type& type)
{
    return type.info() == entt::type_id<glm::vec3>();
}

bool is_glm_quat(const entt::meta_type& type)
{
    return type.info() == entt::type_id<glm::quat>();
}

json meta_any_to_json(const entt::meta_any& any)
{
    if (!any)
        return nullptr;

    const entt::meta_type type = any.type();
    if (!type)
        return nullptr;

    if (type.info() == entt::type_id<bool>())
        return any.cast<bool>();
    if (type.info() == entt::type_id<int>())
        return any.cast<int>();
    if (type.info() == entt::type_id<float>())
        return any.cast<float>();
    if (type.info() == entt::type_id<double>())
        return any.cast<double>();
    if (type.info() == entt::type_id<std::string>())
        return any.cast<std::string>();

    if (is_glm_vec3(type))
        return asset_json::write_vec3(any.cast<glm::vec3>());
    if (is_glm_quat(type))
        return asset_json::write_quat_euler_deg(any.cast<glm::quat>());

    json object = json::object();
    for (auto [id, data] : type.data())
    {
        if (!data.name())
            continue;
        const entt::meta_any member = data.get(any);
        object[data.name()]        = meta_any_to_json(member);
    }
    return object;
}

bool apply_json_value(entt::meta_any& object, const entt::meta_data& data, const json& j)
{
    const entt::meta_type member_type = data.type();
    if (!member_type)
        return false;

    if (member_type.info() == entt::type_id<bool>())
    {
        data.set(object, j.get<bool>());
        return true;
    }
    if (member_type.info() == entt::type_id<int>())
    {
        data.set(object, j.get<int>());
        return true;
    }
    if (member_type.info() == entt::type_id<float>())
    {
        data.set(object, j.get<float>());
        return true;
    }
    if (member_type.info() == entt::type_id<double>())
    {
        data.set(object, j.get<double>());
        return true;
    }
    if (member_type.info() == entt::type_id<std::string>())
    {
        data.set(object, j.get<std::string>());
        return true;
    }
    if (is_glm_vec3(member_type))
    {
        data.set(object, asset_json::read_vec3(j));
        return true;
    }
    if (is_glm_quat(member_type))
    {
        data.set(object, asset_json::read_quat_euler_deg(j));
        return true;
    }

    if (!j.is_object())
        return false;

    entt::meta_any nested = data.get(object);
    if (!nested)
    {
        nested = member_type.construct();
        if (!nested)
            return false;
    }
    meta_json::apply_json(nested, j);
    data.set(object, nested);
    return true;
}
}  // namespace

entt::meta_type meta_json::resolve_type(const std::string_view type_name)
{
    for (auto [id, type] : entt::resolve())
    {
        if (type.name() && type_name == type.name())
            return type;
    }
    return {};
}

nlohmann::json meta_json::to_json(const entt::meta_any& value)
{
    return meta_any_to_json(value);
}

entt::meta_any meta_json::from_json(const std::string_view type_name, const nlohmann::json& j)
{
    const entt::meta_type type = resolve_type(type_name);
    if (!type)
    {
        LOG(WARNING) << "Unknown meta type: " << type_name;
        return {};
    }

    entt::meta_any object = type.construct();
    if (!object)
    {
        LOG(WARNING) << "Failed to construct meta type: " << type_name;
        return {};
    }

    apply_json(object, j);
    return object;
}

void meta_json::apply_json(entt::meta_any& value, const nlohmann::json& j)
{
    if (!value || !j.is_object())
        return;

    const entt::meta_type type = value.type();
    if (!type)
        return;

    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const entt::meta_data data =
            type.data(entt::hashed_string{it.key().c_str(), static_cast<size_t>(it.key().size())});
        if (!data)
        {
            LOG(WARNING) << "Unknown member '" << it.key() << "' on type '" << type.name() << "'";
            continue;
        }
        apply_json_value(value, data, *it);
    }
}
