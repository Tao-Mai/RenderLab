#pragma once

#include <string>

#include <glm/vec3.hpp>

class PointLight
{
  public:
    std::string name{"Point Light"};
    glm::vec3 position{0.0f, 3.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;
    bool enabled = true;
};
