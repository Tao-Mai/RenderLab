#include "asset/AssetImporter.h"

#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/import/AssimpImport.h"
#include "asset/import/GltfImport.h"
#include "asset/import/ImportId.h"
#include "core/Context.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>

namespace
{
bool isGltf(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); }
    );
    return extension == ".gltf" || extension == ".glb";
}
}  // namespace

void asset_import::transformVertices(MeshGeometry& geometry, uint32_t firstVertex, const glm::mat4& transform)
{
    const glm::mat3 linearTransform{transform};
    const float     determinant = glm::determinant(linearTransform);
    CHECK(std::isfinite(determinant) && determinant != 0.0f);
    const glm::mat3 normalMatrix = glm::inverseTranspose(linearTransform);
    const float     handedness   = determinant < 0.0f ? -1.0f : 1.0f;
    for (size_t index = firstVertex; index < geometry.vertices.size(); ++index)
    {
        auto& vertex      = geometry.vertices[index];
        vertex.position   = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
        vertex.normal     = glm::normalize(normalMatrix * vertex.normal);
        glm::vec3 tangent = linearTransform * glm::vec3(vertex.tangent);
        tangent -= vertex.normal * glm::dot(vertex.normal, tangent);
        CHECK(glm::dot(tangent, tangent) > 0.0f, "invalid tangent at vertex {}", index);
        vertex.tangent = glm::vec4(glm::normalize(tangent), vertex.tangent.w * handedness);
    }
}

ModelImport::ModelImport(const std::filesystem::path& path) : source(std::filesystem::absolute(path).lexically_normal())
{
    CHECK(!path.empty());
}

std::shared_ptr<ModelImport> ModelImport::open(const std::filesystem::path& source)
{
    CHECK(!source.empty());
    if (isGltf(source))
        return std::make_shared<GltfImport>(source);

    CHECK(AssimpImport::supports(source), "unsupported model source: {}", source.string());
    return std::make_shared<AssimpImport>(source);
}

bool AssetImporter::supportsModel(const std::filesystem::path& source)
{
    return isGltf(source) || AssimpImport::supports(source);
}

std::string AssetImporter::modelExtensions() { return AssimpImport::extensions() + ";*.gltf;*.glb"; }

AssetImporter::PreparedMesh ModelImport::prepareMesh(std::optional<uint32_t> meshIndex)
{
    const auto start    = std::chrono::steady_clock::now();
    auto       prepared = readMesh(meshIndex);
    prepared.source     = source;
    prepared.importer   = shared_from_this();
    prepared.meshIndex  = meshIndex;

    CHECK(
        !prepared.geometry.vertices.empty() && !prepared.geometry.indices.empty(),
        "model contains no triangle mesh data: {}",
        source.string()
    );
    const auto bounds  = calculateBounds(prepared.geometry);
    prepared.boundsMin = bounds.min;
    prepared.boundsMax = bounds.max;
    LOG_INFO(
        "Mesh preview prepared: {} vertices, {} indices, {} submeshes in {:.1f}s",
        prepared.geometry.vertices.size(),
        prepared.geometry.indices.size(),
        prepared.submeshes.size(),
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
    );
    return prepared;
}

AssetImporter::PreparedMesh
AssetImporter::prepareMesh(const std::filesystem::path& source, std::optional<uint32_t> meshIndex)
{
    return ModelImport::open(source)->prepareMesh(meshIndex);
}

MeshAsset::ID AssetImporter::importMesh(const ImportMeshSetting& setting)
{
    CHECK(std::isfinite(setting.importScale) && setting.importScale > 0.0f);
    return saveMesh(prepareMesh(setting.source), setting);
}

MeshAsset::ID AssetImporter::saveMesh(PreparedMesh&& prepared, const ImportMeshSetting& setting)
{
    DCHECK(prepared.importer);
    auto importer = prepared.importer;
    return importer->saveMesh(std::move(prepared), setting);
}

MeshAsset::ID ModelImport::saveMesh(AssetImporter::PreparedMesh&& prepared, const ImportMeshSetting& setting)
{
    DCHECK(prepared.importer.get() == this);
    CHECK(std::isfinite(setting.importScale) && setting.importScale > 0.0f);
    const auto start  = std::chrono::steady_clock::now();
    const auto meshId = asset_import::nextId<MeshAsset>(setting.id.empty() ? source.stem().string() : setting.id.value);
    if (materialIds.empty())
        materialIds = importMaterials();

    for (auto& vertex : prepared.geometry.vertices)
        vertex.position *= setting.importScale;

    MeshAsset mesh;
    mesh.id          = meshId;
    mesh.source      = Source::File;
    mesh.path        = asset_import::sourcePathForAsset(source);
    mesh.meshIndex   = prepared.meshIndex;
    mesh.importScale = setting.importScale;
    mesh.bounds      = calculateBounds(prepared.geometry);
    mesh.geometry    = context().assetDataManager->writeGeometry(meshId, prepared.geometry);
    mesh.submeshes.reserve(prepared.submeshes.size());
    for (const auto& submesh : prepared.submeshes)
    {
        CHECK(submesh.materialIndex >= -1);
        const auto slot = static_cast<size_t>(submesh.materialIndex + 1);
        CHECK(slot < materialIds.size());
        mesh.submeshes.push_back({submesh.firstIndex, submesh.indexCount, materialIds[slot]});
    }
    const auto id = context().assetManager->save(std::move(mesh));
    LOG_INFO(
        "Import mesh '{}' completed as '{}' in {:.1f}s",
        source.string(),
        id.value,
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
    );
    return id;
}
