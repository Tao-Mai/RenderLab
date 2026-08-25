#include "base/gfx/shader.h"

#include "base/platform/paths.h"

#include <fstream>
#include <iostream>

namespace
{
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        std::cerr << "Failed to open shader: " << path << std::endl;
        return {};
    }
    return {std::istreambuf_iterator<char>(file), {}};
}
}  // namespace

std::filesystem::path Shader::resolve(const std::filesystem::path& path)
{
    if (path.is_absolute())
        return path;
    return paths::shaders_dir() / path;
}

GLuint Shader::compile(GLenum type, const char* src)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        char log[2048];
        glGetShaderInfoLog(shader, 2048, nullptr, log);
        std::cerr << "Shader compile failed:\n" << log << std::endl;
    }
    return shader;
}

Shader::Shader(const std::filesystem::path& vs_path, const std::filesystem::path& fs_path)
{
    const auto vs_resolved = resolve(vs_path);
    const auto fs_resolved = resolve(fs_path);

    const std::string vs_src = read_file(vs_resolved);
    const std::string fs_src = read_file(fs_resolved);

    const GLuint vs = compile(GL_VERTEX_SHADER, vs_src.c_str());
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src.c_str());

    id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glLinkProgram(id);

    GLint success = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &success);
    if (!success)
    {
        char log[2048];
        glGetProgramInfoLog(id, 2048, nullptr, log);
        std::cerr << "Program link failed:\n" << log << std::endl;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

Shader::Shader(Shader&& other) noexcept : id(other.id)
{
    other.id = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept
{
    if (this != &other)
    {
        if (id != 0)
            glDeleteProgram(id);
        id       = other.id;
        other.id = 0;
    }
    return *this;
}

Shader::~Shader()
{
    if (id != 0)
        glDeleteProgram(id);
}

void Shader::use() const
{
    glUseProgram(id);
}

void Shader::set(const std::string& name, const glm::mat4& m) const
{
    const GLint loc = glGetUniformLocation(id, name.c_str());
    if (loc >= 0)
        glProgramUniformMatrix4fv(id, loc, 1, GL_FALSE, glm::value_ptr(m));
}

void Shader::set(const std::string& name, const glm::vec3& v) const
{
    const GLint loc = glGetUniformLocation(id, name.c_str());
    if (loc >= 0)
        glProgramUniform3fv(id, loc, 1, glm::value_ptr(v));
}

void Shader::set(const std::string& name, float v) const
{
    const GLint loc = glGetUniformLocation(id, name.c_str());
    if (loc >= 0)
        glProgramUniform1f(id, loc, v);
}

void Shader::set(const std::string& name, int v) const
{
    const GLint loc = glGetUniformLocation(id, name.c_str());
    if (loc >= 0)
        glProgramUniform1i(id, loc, v);
}
