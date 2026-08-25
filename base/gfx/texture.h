#pragma once

#include <glad/glad.h>

#include <glm/glm.hpp>

#include <utility>

// 2D 纹理 GPU 句柄（DSA 创建）。纯色 1×1 纹理用于 builtin:color/... 等。
class TextureGPU
{
public:
    GLuint id = 0;

    TextureGPU() = default;

    static TextureGPU create_solid(const glm::vec4& rgba);

    void destroy()
    {
        if (id != 0)
            glDeleteTextures(1, &id);
        id = 0;
    }

    void bind_unit(GLuint unit) const
    {
        if (id != 0)
            glBindTextureUnit(unit, id);
    }

    [[nodiscard]] bool valid() const { return id != 0; }

    ~TextureGPU() { destroy(); }

    TextureGPU(const TextureGPU&)            = delete;
    TextureGPU& operator=(const TextureGPU&) = delete;

    TextureGPU(TextureGPU&& other) noexcept : id(other.id) { other.id = 0; }

    TextureGPU& operator=(TextureGPU&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            id       = other.id;
            other.id = 0;
        }
        return *this;
    }
};
