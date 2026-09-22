#pragma once

#include <string>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct MaterialData
{
    std::string name = "Default";
    glm::vec4   baseColorFactor{1.0f};
    float       metallic    = 1.0f;
    float       roughness   = 1.0f;
    float       ao          = 1.0f;
    glm::vec3   emissive{0.0f};
    float       normalScale = 1.0f;

    std::string albedoMap;
    std::string normalMap;
    std::string metallicMap;
    std::string roughnessMap;
    std::string aoMap;
    std::string emissiveMap;

    std::string alphaMode   = "OPAQUE";
    float       alphaCutoff = 0.5f;
    bool        doubleSided = false;

    [[nodiscard]] bool hasAlbedoMap() const { return !albedoMap.empty(); }
};
