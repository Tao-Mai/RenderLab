#ifndef RENDERLAB_PBRT_MATERIAL_PROGRAM_H
#define RENDERLAB_PBRT_MATERIAL_PROGRAM_H

#ifdef __cplusplus
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#define RENDERLAB_PBRT_UINT uint32_t
#define RENDERLAB_PBRT_CONSTANT inline constexpr uint32_t
#else
#define RENDERLAB_PBRT_UINT uint
#define RENDERLAB_PBRT_CONSTANT static const uint
#endif

namespace pbrt
{
RENDERLAB_PBRT_CONSTANT kTextureStackSize  = 64;
RENDERLAB_PBRT_CONSTANT kMaterialStackSize = 64;

enum class TextureOp : RENDERLAB_PBRT_UINT
{
    Program, Constant, Image, Multiply, Mix, Checkerboard, Bilerp, Dots, DirectionMix,
    Fbm, Wrinkled, Windy, Marble, PtexPreview,
};

enum class TextureMapping : RENDERLAB_PBRT_UINT
{
    Uv, Spherical, Cylindrical, Planar, Spatial,
};

enum class MaterialModel : RENDERLAB_PBRT_UINT
{
    Diffuse,
    SmoothDielectric,
    Color,
    Conductor,
    DiffuseTransmission,
    Subsurface,
    HairAbsorption,
    HairPigments,
    Mix,
    ConductorColor,
};

#ifdef __cplusplus
struct alignas(16) TextureInstruction
{
    bool operator==(const TextureInstruction&) const = default;

    glm::uvec4 metadata{};  // Operation, scalar output / program length, texture, sampler.
    glm::vec4  value{};
    glm::vec4  uvScaleOffset{1, 1, 0, 0};
    glm::vec4  uAxisKind{1, 0, 0, 0};
    glm::vec4  vAxis{0, 1, 0, 0};
    glm::vec4  settings{};
    glm::mat4  textureFromWorld{1};
};
static_assert(sizeof(TextureInstruction) == 160);

struct alignas(16) MaterialInstruction
{
    bool operator==(const MaterialInstruction&) const = default;

    MaterialModel model            = MaterialModel::Diffuse;
    uint32_t      firstTexture     = 0;
    uint32_t      secondTexture    = 0;
    uint32_t      instructionCount = 0;
    glm::uvec4    inputs{};  // Roughness U / scale, roughness V, remap roughness, reflectance input.
};
static_assert(sizeof(MaterialInstruction) == 32);
#endif
}

#undef RENDERLAB_PBRT_UINT
#undef RENDERLAB_PBRT_CONSTANT
#endif
