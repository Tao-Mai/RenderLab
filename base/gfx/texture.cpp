#include "base/gfx/texture.h"

#include <glm/common.hpp>

TextureGPU TextureGPU::create_solid(const glm::vec4& rgba)
{
    TextureGPU tex;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex.id);

    glTextureStorage2D(tex.id, 1, GL_RGBA8, 1, 1);

    const unsigned char px[] = {
        static_cast<unsigned char>(glm::clamp(rgba.r, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(rgba.g, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(rgba.b, 0.0f, 1.0f) * 255.0f),
        static_cast<unsigned char>(glm::clamp(rgba.a, 0.0f, 1.0f) * 255.0f),
    };
    glTextureSubImage2D(tex.id, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);

    glTextureParameteri(tex.id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(tex.id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(tex.id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex.id, GL_TEXTURE_WRAP_T, GL_REPEAT);

    return tex;
}
