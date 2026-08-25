#include "base/gfx/mesh_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glog/logging.h>

#include <algorithm>
#include <limits>

std::vector<Mesh> LoadMeshes(const std::filesystem::path& path)
{
    Assimp::Importer importer;

    constexpr unsigned int flags =
        aiProcess_Triangulate |
        aiProcess_FlipUVs |
        aiProcess_GenNormals |
        aiProcess_GlobalScale;

    const auto* scene = importer.ReadFile(path.string(), flags);
    CHECK(scene) << "Failed to load model: " << path << " (" << importer.GetErrorString() << ")";

    std::vector<Mesh> meshes;

    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();

    for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
    {
        const auto* ai_mesh = scene->mMeshes[i];
        Mesh        mesh;
        mesh.vertices.resize(ai_mesh->mNumVertices);

        for (unsigned int j = 0; j < ai_mesh->mNumVertices; ++j)
        {
            const glm::vec3 pos{
                ai_mesh->mVertices[j].x,
                ai_mesh->mVertices[j].y,
                ai_mesh->mVertices[j].z,
            };
            mesh.vertices[j].pos = pos;
            min_x = std::min(min_x, pos.x);
            max_x = std::max(max_x, pos.x);
            min_y = std::min(min_y, pos.y);
            max_y = std::max(max_y, pos.y);
            min_z = std::min(min_z, pos.z);
            max_z = std::max(max_z, pos.z);
        }

        if (ai_mesh->HasNormals())
        {
            for (unsigned int j = 0; j < ai_mesh->mNumVertices; ++j)
            {
                mesh.vertices[j].normal = {
                    ai_mesh->mNormals[j].x,
                    ai_mesh->mNormals[j].y,
                    ai_mesh->mNormals[j].z,
                };
            }
        }

        if (ai_mesh->HasTextureCoords(0))
        {
            for (unsigned int j = 0; j < ai_mesh->mNumVertices; ++j)
            {
                mesh.vertices[j].uv = {
                    ai_mesh->mTextureCoords[0][j].x,
                    ai_mesh->mTextureCoords[0][j].y,
                };
            }
        }

        for (unsigned int j = 0; j < ai_mesh->mNumFaces; ++j)
        {
            const auto* ai_indices = ai_mesh->mFaces[j].mIndices;
            mesh.indices.push_back(ai_indices[0]);
            mesh.indices.push_back(ai_indices[1]);
            mesh.indices.push_back(ai_indices[2]);
        }

        meshes.push_back(std::move(mesh));
    }

    const glm::vec3 center{(min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f, (min_z + max_z) * 0.5f};
    for (auto& mesh : meshes)
        for (auto& vert : mesh.vertices)
            vert.pos -= center;

    return meshes;
}
