#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <string>

// 填 vert/frag 路径即可编译链接；相对路径默认相对 paths::shaders_dir()。
class Shader
{
public:
    GLuint id = 0;

    Shader() = default;
    Shader(const std::filesystem::path& vs_path, const std::filesystem::path& fs_path);

    Shader(const Shader&)            = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    ~Shader();

    void use() const;

    void set(const std::string& name, const glm::mat4& m) const;
    void set(const std::string& name, const glm::vec3& v) const;
    void set(const std::string& name, float v) const;
    void set(const std::string& name, int v) const;

    // 旧名
    void set_mat4(const std::string& name, const glm::mat4& m) const { set(name, m); }
    void set_vec3(const std::string& name, const glm::vec3& v) const { set(name, v); }
    void set_float(const std::string& name, float v) const { set(name, v); }
    void set_int(const std::string& name, int v) const { set(name, v); }

private:
    static std::filesystem::path resolve(const std::filesystem::path& path);
    static GLuint                compile(GLenum type, const char* src);
};
