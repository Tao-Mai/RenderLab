#include "asset/MeshGeometry.h"

#include "core/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

#include <mikktspace.h>
#include <glm/geometric.hpp>

namespace
{
struct TangentBuild
{
    const MeshGeometry& mesh;
    uint32_t firstIndex;
    std::vector<glm::vec4> tangents;

    const Vertex& vertex(int face, int corner) const
    {
        return mesh.vertices[mesh.indices[firstIndex + face * 3 + corner]];
    }
};
}

void generateTangents(MeshGeometry& mesh, uint32_t firstIndex, uint32_t indexCount)
{
    DCHECK(firstIndex <= mesh.indices.size() && indexCount <= mesh.indices.size() - firstIndex);
    CHECK(indexCount > 0 && indexCount % 3 == 0);
    CHECK(indexCount / 3 <= static_cast<uint32_t>(std::numeric_limits<int>::max()));
    for (uint32_t index = firstIndex; index < firstIndex + indexCount; ++index)
        CHECK(mesh.indices[index] < mesh.vertices.size());

    TangentBuild build{mesh, firstIndex, std::vector<glm::vec4>(indexCount)};
    SMikkTSpaceInterface interface{};
    interface.m_getNumFaces = [](const SMikkTSpaceContext* context)
    {
        return static_cast<int>(static_cast<TangentBuild*>(context->m_pUserData)->tangents.size() / 3);
    };
    interface.m_getNumVerticesOfFace = [](const SMikkTSpaceContext*, int) { return 3; };
    interface.m_getPosition = [](const SMikkTSpaceContext* context, float out[], int face, int corner)
    {
        const auto& value = static_cast<TangentBuild*>(context->m_pUserData)->vertex(face, corner).position;
        out[0] = value.x;
        out[1] = value.y;
        out[2] = value.z;
    };
    interface.m_getNormal = [](const SMikkTSpaceContext* context, float out[], int face, int corner)
    {
        const auto& value = static_cast<TangentBuild*>(context->m_pUserData)->vertex(face, corner).normal;
        out[0] = value.x;
        out[1] = value.y;
        out[2] = value.z;
    };
    interface.m_getTexCoord = [](const SMikkTSpaceContext* context, float out[], int face, int corner)
    {
        const auto& value = static_cast<TangentBuild*>(context->m_pUserData)->vertex(face, corner).texcoord;
        out[0] = value.x;
        out[1] = value.y;
    };
    interface.m_setTSpaceBasic = [](
        const SMikkTSpaceContext* context, const float tangent[], float sign, int face, int corner)
    {
        auto& build = *static_cast<TangentBuild*>(context->m_pUserData);
        glm::vec3 direction{tangent[0], tangent[1], tangent[2]};
        const auto& normal = build.vertex(face, corner).normal;
        const glm::vec3 projected = direction - normal * glm::dot(normal, direction);
        if (glm::dot(projected, projected) < 1e-12f)
        {
            // Degenerate UVs have no defined tangent; keep their frame usable.
            const glm::vec3 axis = std::abs(normal.z) < 0.999f
                ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
            direction = glm::normalize(glm::cross(axis, normal));
        }
        build.tangents[face * 3 + corner] = glm::vec4(direction, sign);
    };
    const SMikkTSpaceContext mikkContext{&interface, &build};
    CHECK(genTangSpaceDefault(&mikkContext));

    // MikkTSpace returns per-corner tangents. Split shared vertices at tangent
    // discontinuities instead of overwriting or averaging their frames.
    std::unordered_map<uint32_t, std::vector<std::pair<glm::vec4, uint32_t>>> variants;
    for (uint32_t corner = 0; corner < indexCount; ++corner)
    {
        auto& index = mesh.indices[firstIndex + corner];
        auto& choices = variants[index];
        const auto tangent = build.tangents[corner];
        const auto found = std::ranges::find_if(choices, [&](const auto& choice)
        {
            return choice.first == tangent;
        });
        if (found != choices.end())
        {
            index = found->second;
            continue;
        }

        uint32_t destination = index;
        if (!choices.empty())
        {
            CHECK(mesh.vertices.size() < std::numeric_limits<uint32_t>::max());
            Vertex duplicate = mesh.vertices[index];
            destination = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(duplicate);
        }
        mesh.vertices[destination].tangent = tangent;
        choices.emplace_back(tangent, destination);
        index = destination;
    }
}
