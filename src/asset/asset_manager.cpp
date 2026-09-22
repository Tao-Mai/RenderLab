#include "asset/asset_manager.h"

#include "asset/builtin_meshes.h"
#include "asset/gltf_loader.h"
#include "asset/json_io.h"
#include "logger.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <utility>

namespace
{
constexpr uint32_t geometryMagic = 0x4853454du;
constexpr uint32_t geometryVersion = 1;

std::string idFileStem(const AssetId& id)
{
    std::string stem = id;
    for (char& character : stem)
    {
        if (character == ':' || character == '/' || character == '\\')
        {
            character = '_';
        }
    }
    return stem;
}

void writeGeometry(const std::filesystem::path& file, const ImportedMesh& mesh)
{
    CHECK(!mesh.vertices.empty() && !mesh.indices.empty(),
        "imported mesh has no geometry: {}", file.string());
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    CHECK(output, "cannot write mesh geometry: {}", file.string());
    const std::array<uint32_t, 4> header{
        geometryMagic,
        geometryVersion,
        static_cast<uint32_t>(mesh.vertices.size()),
        static_cast<uint32_t>(mesh.indices.size()),
    };
    output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
    for (const Vertex& vertex : mesh.vertices)
    {
        const std::array<float, 8> values{
            vertex.position.x, vertex.position.y, vertex.position.z,
            vertex.normal.x, vertex.normal.y, vertex.normal.z,
            vertex.texcoord.x, vertex.texcoord.y,
        };
        output.write(reinterpret_cast<const char*>(values.data()), sizeof(values));
    }
    output.write(
        reinterpret_cast<const char*>(mesh.indices.data()),
        static_cast<std::streamsize>(mesh.indices.size() * sizeof(uint32_t)));
    CHECK(output, "failed writing mesh geometry: {}", file.string());
}

template <class T, class Validate>
void loadDescDirectory(
    const std::filesystem::path& directory,
    AssetType expectedType,
    std::unordered_map<AssetId, T>& out,
    std::unordered_map<AssetId, std::filesystem::path>& descFiles,
    Validate&& validate)
{
    if (!std::filesystem::exists(directory))
    {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        T desc = asset_json::load<T>(entry.path());
        CHECK(desc.asset().type == expectedType,
            "descriptor type mismatch in {}: expected {}, got {}",
            entry.path().string(),
            assetTypeDir(expectedType),
            assetTypeDir(desc.asset().type));
        validate(desc, entry.path());
        CHECK(out.emplace(desc.asset().id, desc).second,
            "duplicate asset ID: {}", desc.asset().id);
        descFiles.insert_or_assign(desc.asset().id, entry.path());
    }
}
}

AssetManager::AssetManager(std::filesystem::path assetRoot) :
    root(std::filesystem::absolute(std::move(assetRoot)).lexically_normal())
{
    loadAll();
}

void AssetManager::storeDescFile(const AssetId& id, const std::filesystem::path& file)
{
    descFiles.insert_or_assign(id, file);
}

std::filesystem::path AssetManager::descFile(AssetType type, const AssetId& id) const
{
    if (const auto found = descFiles.find(id); found != descFiles.end())
    {
        return found->second;
    }
    return root / "descs" / std::string(assetTypeDir(type)) / (idFileStem(id) + ".json");
}

void AssetManager::loadAll()
{
    clear();
    loadDescDirectory(
        root / "descs" / "mesh",
        AssetType::Mesh,
        meshes,
        descFiles,
        [](const MeshDesc& desc, const std::filesystem::path& file)
        {
            CHECK(!desc.source.kind.empty(),
                "mesh descriptor missing source.kind: {}", file.string());
        });
    loadDescDirectory(
        root / "descs" / "material",
        AssetType::Material,
        materials,
        descFiles,
        [](const MaterialDesc& desc, const std::filesystem::path& file)
        {
            CHECK(!desc.baseColorTexture.empty(),
                "material requires baseColorTexture: {}", file.string());
        });
    loadDescDirectory(
        root / "descs" / "texture",
        AssetType::Texture,
        textures,
        descFiles,
        [](const TextureDesc& desc, const std::filesystem::path& file)
        {
            if (desc.source == "file")
            {
                CHECK(desc.path.has_value() && !desc.path->empty(),
                    "texture path is empty: {}", file.string());
            }
            else if (desc.source == "solid")
            {
                CHECK(desc.rgba.has_value(),
                    "solid texture requires four color bytes: {}", file.string());
            }
            else
            {
                LOG_FATAL("unknown texture source in {}: {}", file.string(), desc.source);
            }
        });
    loadDescDirectory(
        root / "descs" / "shader",
        AssetType::Shader,
        shaders,
        descFiles,
        [](const ShaderDesc& desc, const std::filesystem::path& file)
        {
            CHECK(!desc.binary.empty(), "invalid shader descriptor: {}", file.string());
        });
}

void AssetManager::reload()
{
    loadAll();
}

const MeshDesc& AssetManager::meshDesc(const AssetId& id) const
{
    const auto found = meshes.find(id);
    CHECK(found != meshes.end(), "mesh descriptor not loaded: {}", id);
    return found->second;
}

const MaterialDesc& AssetManager::materialDesc(const AssetId& id) const
{
    const auto found = materials.find(id);
    CHECK(found != materials.end(), "material descriptor not loaded: {}", id);
    return found->second;
}

const TextureDesc& AssetManager::textureDesc(const AssetId& id) const
{
    const auto found = textures.find(id);
    CHECK(found != textures.end(), "texture descriptor not loaded: {}", id);
    return found->second;
}

const ShaderDesc& AssetManager::shaderDesc(const AssetId& id) const
{
    const auto found = shaders.find(id);
    CHECK(found != shaders.end(), "shader descriptor not loaded: {}", id);
    return found->second;
}

MeshGeometry AssetManager::readGeometryFile(const std::filesystem::path& file) const
{
    std::ifstream input(file, std::ios::binary);
    CHECK(input, "cannot open mesh geometry: {}", file.string());
    std::array<uint32_t, 4> header{};
    input.read(reinterpret_cast<char*>(header.data()), sizeof(header));
    CHECK(input && header[0] == geometryMagic && header[1] == geometryVersion,
        "invalid mesh geometry header: {}", file.string());
    const uint64_t expectedSize = sizeof(header) +
        static_cast<uint64_t>(header[2]) * 8 * sizeof(float) +
        static_cast<uint64_t>(header[3]) * sizeof(uint32_t);
    CHECK(std::filesystem::file_size(file) == expectedSize && header[2] != 0 && header[3] != 0,
        "invalid mesh geometry size: {}", file.string());
    MeshGeometry geometry;
    geometry.vertices.reserve(header[2]);
    geometry.indices.resize(header[3]);
    for (uint32_t index = 0; index < header[2]; ++index)
    {
        std::array<float, 8> values{};
        input.read(reinterpret_cast<char*>(values.data()), sizeof(values));
        geometry.vertices.push_back({
            .position = {values[0], values[1], values[2]},
            .normal = {values[3], values[4], values[5]},
            .texcoord = {values[6], values[7]},
        });
    }
    input.read(
        reinterpret_cast<char*>(geometry.indices.data()),
        static_cast<std::streamsize>(geometry.indices.size() * sizeof(uint32_t)));
    CHECK(input, "failed reading mesh geometry: {}", file.string());
    return geometry;
}

MeshGeometry AssetManager::loadGeometry(const AssetId& id)
{
    auto found = meshes.find(id);
    CHECK(found != meshes.end(), "mesh descriptor not loaded: {}", id);
    MeshDesc& desc = found->second;
    const bool missingGeometry = desc.geometry.empty() ||
        !std::filesystem::exists(path(desc.geometry));
    if (missingGeometry)
    {
        CHECK(!desc.source.kind.empty(),
            "mesh '{}' has no geometry and no import source", id);
        importMesh(id, desc.source);
        found = meshes.find(id);
        CHECK(found != meshes.end(), "mesh descriptor missing after reimport: {}", id);
    }
    return readGeometryFile(path(found->second.geometry));
}

std::filesystem::path AssetManager::path(const std::filesystem::path& relative) const
{
    CHECK(!relative.is_absolute(), "asset path must be relative: {}", relative.string());
    return root / relative;
}

void AssetManager::importMesh(const AssetId& id, MeshSourceDesc source)
{
    if (source.kind == "gltf")
    {
        CHECK(source.path.has_value() && !source.path->empty(),
            "gltf mesh source requires path: {}", id);
    }
    else
    {
        CHECK(source.kind == "cube" || source.kind == "sphere" || source.kind == "arrow",
            "unknown mesh import kind: {}", source.kind);
    }

    ImportedMesh imported;
    if (source.kind == "gltf")
    {
        imported = GLTFLoader::load(path(*source.path));
    }
    else if (source.kind == "cube")
    {
        imported = BuiltinMeshes::cube();
    }
    else if (source.kind == "sphere")
    {
        imported = BuiltinMeshes::sphere();
    }
    else if (source.kind == "arrow")
    {
        imported = BuiltinMeshes::arrow();
    }

    const std::string meshName = id.contains(':') ? id.substr(id.find(':') + 1) : id;
    std::unordered_map<std::string, AssetId> textureIds;
    auto textureIdFor = [&](const std::string& sourcePath) -> AssetId
    {
        if (sourcePath.empty())
        {
            return {};
        }
        CHECK(!sourcePath.starts_with("data:") && !sourcePath.starts_with("embedded:"),
            "embedded glTF textures require external files: {}", id);
        if (const auto found = textureIds.find(sourcePath); found != textureIds.end())
        {
            return found->second;
        }
        const AssetId textureId =
            "texture:" + meshName + ":" + std::to_string(textureIds.size());
        const auto relative =
            std::filesystem::relative(std::filesystem::absolute(sourcePath), root);
        CHECK(!relative.empty() && *relative.begin() != "..",
            "texture is outside asset root: {}", sourcePath);
        TextureDesc textureDesc;
        textureDesc.asset().id = textureId;
        textureDesc.asset().name = relative.filename().string();
        textureDesc.asset().type = AssetType::Texture;
        textureDesc.source = "file";
        textureDesc.path = relative.generic_string();
        const auto file = descFile(AssetType::Texture, textureId);
        asset_json::save(file, textureDesc);
        storeDescFile(textureId, file);
        textures.insert_or_assign(textureId, textureDesc);
        textureIds.emplace(sourcePath, textureId);
        return textureId;
    };

    std::vector<AssetId> materialIds;
    materialIds.reserve(imported.materials.size());
    for (size_t index = 0; index < imported.materials.size(); ++index)
    {
        const ImportedMaterial& material = imported.materials[index];
        const AssetId materialId = "material:" + meshName + ":" + std::to_string(index);
        AssetId baseColorTexture = textureIdFor(material.albedoMap);
        if (baseColorTexture.empty())
        {
            baseColorTexture = "texture:white";
        }
        MaterialDesc materialDesc;
        materialDesc.asset().id = materialId;
        materialDesc.asset().name = material.name;
        materialDesc.asset().type = AssetType::Material;
        materialDesc.baseColorFactor = material.baseColorFactor;
        materialDesc.metallic = material.metallic;
        materialDesc.roughness = material.roughness;
        materialDesc.ao = material.ao;
        materialDesc.emissive = material.emissive;
        materialDesc.normalScale = material.normalScale;
        materialDesc.baseColorTexture = baseColorTexture;
        materialDesc.normalTexture = textureIdFor(material.normalMap);
        materialDesc.metallicTexture = textureIdFor(material.metallicMap);
        materialDesc.roughnessTexture = textureIdFor(material.roughnessMap);
        materialDesc.aoTexture = textureIdFor(material.aoMap);
        materialDesc.emissiveTexture = textureIdFor(material.emissiveMap);
        materialDesc.alphaMode = material.alphaMode;
        materialDesc.alphaCutoff = material.alphaCutoff;
        materialDesc.doubleSided = material.doubleSided;
        const auto file = descFile(AssetType::Material, materialId);
        asset_json::save(file, materialDesc);
        storeDescFile(materialId, file);
        materials.insert_or_assign(materialId, materialDesc);
        materialIds.push_back(materialId);
    }

    const auto geometryRelative =
        std::filesystem::path("geometry") / (idFileStem(id) + ".bin");
    writeGeometry(path(geometryRelative), imported);

    MeshDesc meshDesc;
    meshDesc.asset().id = id;
    meshDesc.asset().name = meshName;
    meshDesc.asset().type = AssetType::Mesh;
    meshDesc.source = std::move(source);
    meshDesc.geometry = geometryRelative.generic_string();
    meshDesc.submeshes.reserve(imported.submeshes.size());
    for (const ImportedSubmesh& submesh : imported.submeshes)
    {
        CHECK(submesh.materialIndex < materialIds.size(),
            "submesh has invalid material index: {}", id);
        meshDesc.submeshes.push_back({
            .firstIndex = submesh.firstIndex,
            .indexCount = submesh.indexCount,
            .materialId = materialIds[submesh.materialIndex],
        });
    }
    const auto meshFile = descFile(AssetType::Mesh, id);
    asset_json::save(meshFile, meshDesc);
    storeDescFile(id, meshFile);
    meshes.insert_or_assign(id, std::move(meshDesc));
}

void AssetManager::clear()
{
    shaders.clear();
    textures.clear();
    materials.clear();
    meshes.clear();
    descFiles.clear();
}
