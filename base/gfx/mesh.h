#pragma once

#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <vector>

#include <glm/glm.hpp>

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct Mesh
{
    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;
};

struct Material
{
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};
    glm::vec3 specular{0.5f, 0.5f, 0.5f};
    float     shininess = 32.0f;
};

class MeshFactory
{
public:
    [[nodiscard]] static Mesh createCuboid(float width, float height, float depth)
    {
        if (width <= 0.0f || height <= 0.0f || depth <= 0.0f)
            throw std::invalid_argument("Cuboid dimensions must be positive");

        const float x = width * 0.5f;
        const float y = height * 0.5f;
        const float z = depth * 0.5f;

        struct Face
        {
            std::array<glm::vec3, 4> positions;
            glm::vec3                normal;
        };

        const std::array<Face, 6> faces = {{
            {{{{-x, -y, +z}, {+x, -y, +z}, {+x, +y, +z}, {-x, +y, +z}}}, {0.0f, 0.0f, +1.0f}},
            {{{{+x, -y, -z}, {-x, -y, -z}, {-x, +y, -z}, {+x, +y, -z}}}, {0.0f, 0.0f, -1.0f}},
            {{{{+x, -y, +z}, {+x, -y, -z}, {+x, +y, -z}, {+x, +y, +z}}}, {+1.0f, 0.0f, 0.0f}},
            {{{{-x, -y, -z}, {-x, -y, +z}, {-x, +y, +z}, {-x, +y, -z}}}, {-1.0f, 0.0f, 0.0f}},
            {{{{-x, +y, +z}, {+x, +y, +z}, {+x, +y, -z}, {-x, +y, -z}}}, {0.0f, +1.0f, 0.0f}},
            {{{{-x, -y, -z}, {+x, -y, -z}, {+x, -y, +z}, {-x, -y, +z}}}, {0.0f, -1.0f, 0.0f}},
        }};

        constexpr std::array<glm::vec2, 4> tex_coords = {
            glm::vec2{0.0f, 0.0f}, glm::vec2{1.0f, 0.0f},
            glm::vec2{1.0f, 1.0f}, glm::vec2{0.0f, 1.0f},
        };

        Mesh mesh;
        mesh.vertices.reserve(24);
        mesh.indices.reserve(36);

        for (const Face& face : faces)
        {
            const auto base = static_cast<unsigned int>(mesh.vertices.size());
            for (std::size_t i = 0; i < face.positions.size(); ++i)
                mesh.vertices.push_back({face.positions[i], face.normal, tex_coords[i]});

            mesh.indices.insert(mesh.indices.end(), {
                                    base, base + 1, base + 2,
                                    base, base + 2, base + 3,
                                });
        }
        return mesh;
    }

    [[nodiscard]] static Mesh createSphere(float        radius,
                                           unsigned int sectors = 32,
                                           unsigned int stacks  = 16)
    {
        if (radius <= 0.0f)
            throw std::invalid_argument("Sphere radius must be positive");
        if (sectors < 3)
            throw std::invalid_argument("A sphere requires at least 3 sectors");
        if (stacks < 2)
            throw std::invalid_argument("A sphere requires at least 2 stacks");

        Mesh mesh;
        mesh.vertices.reserve(static_cast<std::size_t>(stacks + 1) * (sectors + 1));
        mesh.indices.reserve(static_cast<std::size_t>(6) * sectors * (stacks - 1));

        constexpr float pi = std::numbers::pi_v<float>;
        for (unsigned int stack = 0; stack <= stacks; ++stack)
        {
            const float v       = static_cast<float>(stack) / static_cast<float>(stacks);
            const float phi     = pi * v;
            const float sin_phi = std::sin(phi);
            const float cos_phi = std::cos(phi);

            for (unsigned int sector = 0; sector <= sectors; ++sector)
            {
                const float     u     = static_cast<float>(sector) / static_cast<float>(sectors);
                const float     theta = 2.0f * pi * u;
                const glm::vec3 normal{
                    sin_phi * std::cos(theta),
                    cos_phi,
                    sin_phi * std::sin(theta),
                };
                mesh.vertices.push_back({radius * normal, normal, {u, 1.0f - v}});
            }
        }

        const unsigned int row_size = sectors + 1;
        for (unsigned int stack = 0; stack < stacks; ++stack)
        {
            const unsigned int upper = stack * row_size;
            const unsigned int lower = upper + row_size;

            for (unsigned int sector = 0; sector < sectors; ++sector)
            {
                if (stack != 0)
                {
                    mesh.indices.push_back(upper + sector);
                    mesh.indices.push_back(upper + sector + 1);
                    mesh.indices.push_back(lower + sector + 1);
                }
                if (stack != stacks - 1)
                {
                    mesh.indices.push_back(upper + sector);
                    mesh.indices.push_back(lower + sector + 1);
                    mesh.indices.push_back(lower + sector);
                }
            }
        }
        return mesh;
    }

    static Mesh createPlane(float width, float height)
    {
        Mesh mesh;
        mesh.vertices.resize(4);
        mesh.vertices[0] = {{-width / 2, 0.0f, -height / 2}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}};
        mesh.vertices[1] = {{width / 2, 0.0f, -height / 2}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}};
        mesh.vertices[2] = {{width / 2, 0.0f, height / 2}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}};
        mesh.vertices[3] = {{-width / 2, 0.0f, height / 2}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}};
        mesh.indices     = {0, 1, 2, 2, 3, 0};
        return mesh;
    }
};
