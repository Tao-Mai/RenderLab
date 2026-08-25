#pragma once

#include <glm/glm.hpp>

#include <string>

// 场景 JSON 可序列化的网格组件（运行时转为 MeshRenderer + GPU 资源）
struct MeshRendererDesc
{
    std::string mesh = "builtin:cube";
    glm::vec3   albedo{1.0f, 1.0f, 1.0f};
    glm::vec3   specular{0.5f, 0.5f, 0.5f};
    float       shininess = 32.0f;
    std::string albedo_texture;
};

#include "base/core/reflection/meta_register.h"

META_REGISTER(MeshRendererDesc, MeshRenderer, mesh, albedo, specular, shininess, albedo_texture);
