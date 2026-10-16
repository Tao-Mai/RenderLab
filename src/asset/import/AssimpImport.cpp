#include "asset/import/AssimpImport.h"

#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "asset/TextureMip.h"
#include "asset/import/ImportId.h"
#include "core/Context.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <map>
#include <tuple>

namespace
{
constexpr unsigned kImportFlags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals |
    aiProcess_SortByPType | aiProcess_FlipUVs | aiProcess_TransformUVCoords | aiProcess_ValidateDataStructure;

glm::vec3 toGlm(const aiVector3D& vector) { return {vector.x, vector.y, vector.z}; }

glm::mat4 toGlm(const aiMatrix4x4& matrix)
{
    return {
        matrix.a1,
        matrix.b1,
        matrix.c1,
        matrix.d1,
        matrix.a2,
        matrix.b2,
        matrix.c2,
        matrix.d2,
        matrix.a3,
        matrix.b3,
        matrix.c3,
        matrix.d3,
        matrix.a4,
        matrix.b4,
        matrix.c4,
        matrix.d4,
    };
}

void appendMesh(AssetImporter::PreparedMesh& build, const aiMesh& mesh, const glm::mat4& transform)
{
    if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
        return;
    CHECK(!mesh.HasBones(), "only static meshes are supported: {}", build.source.string());
    CHECK(mesh.HasPositions() && mesh.HasNormals());
    CHECK(build.geometry.vertices.size() + mesh.mNumVertices <= std::numeric_limits<uint32_t>::max());
    CHECK(build.geometry.indices.size() + uint64_t{mesh.mNumFaces} * 3 <= std::numeric_limits<uint32_t>::max());
    const auto            firstVertex     = static_cast<uint32_t>(build.geometry.vertices.size());
    const auto            firstIndex      = static_cast<uint32_t>(build.geometry.indices.size());
    constexpr auto        kUnmappedVertex = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> vertices(mesh.mNumVertices, kUnmappedVertex);
    uint32_t              degenerateFaces = 0;

    for (uint32_t faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
    {
        const auto& face = mesh.mFaces[faceIndex];
        CHECK(face.mNumIndices == 3);
        for (uint32_t corner = 0; corner < 3; ++corner)
            CHECK(face.mIndices[corner] < mesh.mNumVertices);
        const auto  a           = toGlm(mesh.mVertices[face.mIndices[0]]);
        const auto  b           = toGlm(mesh.mVertices[face.mIndices[1]]);
        const auto  c           = toGlm(mesh.mVertices[face.mIndices[2]]);
        const auto  faceNormal  = glm::cross(b - a, c - a);
        const float areaSquared = glm::dot(faceNormal, faceNormal);
        CHECK(
            std::isfinite(areaSquared),
            "invalid triangle in mesh '{}': {}",
            mesh.mName.C_Str(),
            build.source.string()
        );
        // Discard zero-area faces, not small valid triangles. Their generated normals can contain NaNs.
        if (areaSquared == 0.0f)
        {
            ++degenerateFaces;
            continue;
        }

        for (uint32_t corner = 0; corner < 3; ++corner)
        {
            const auto sourceIndex = face.mIndices[corner];
            auto&      destination = vertices[sourceIndex];
            if (destination == kUnmappedVertex)
            {
                const auto  sourceNormal        = toGlm(mesh.mNormals[sourceIndex]);
                const float normalLengthSquared = glm::dot(sourceNormal, sourceNormal);
                CHECK(
                    normalLengthSquared > 0 && std::isfinite(normalLengthSquared),
                    "invalid normal at vertex {} in mesh '{}': {}",
                    sourceIndex,
                    mesh.mName.C_Str(),
                    build.source.string()
                );
                const auto normal = glm::normalize(sourceNormal);
                const auto axis   = std::abs(normal.z) < 0.999f ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
                destination       = static_cast<uint32_t>(build.geometry.vertices.size());
                build.geometry.vertices.push_back({
                    .position = toGlm(mesh.mVertices[sourceIndex]),
                    .normal   = normal,
                    .texcoord =
                        mesh.HasTextureCoords(0) ? glm::vec2{toGlm(mesh.mTextureCoords[0][sourceIndex])} : glm::vec2{0},
                    .tangent = glm::vec4{glm::normalize(glm::cross(axis, normal)), 1.0f},
                });
            }
            build.geometry.indices.push_back(destination);
        }
    }
    if (degenerateFaces != 0)
        LOG_WARNING(
            "Removed {} zero-area triangles from mesh '{}': {}",
            degenerateFaces,
            mesh.mName.C_Str(),
            build.source.string()
        );
    const auto indexCount = static_cast<uint32_t>(build.geometry.indices.size()) - firstIndex;
    if (indexCount == 0)
        return;
    generateTangents(build.geometry, firstIndex, indexCount);
    asset_import::transformVertices(build.geometry, firstVertex, transform);
    if (glm::determinant(glm::mat3{transform}) < 0)
        for (uint32_t index = firstIndex; index < firstIndex + indexCount; index += 3)
            std::swap(build.geometry.indices[index + 1], build.geometry.indices[index + 2]);
    build.submeshes.push_back({firstIndex, indexCount, static_cast<int>(mesh.mMaterialIndex)});
}

struct MaterialImport
{
    const aiScene&                                                        scene;
    const std::filesystem::path&                                          source;
    std::map<std::tuple<std::string, ColorSpace, bool>, TextureAsset::ID> textures;
    std::map<std::array<aiTextureMapMode, 3>, SamplerAsset::ID>           samplers;

    TextureAsset::ID importTexture(const aiString& reference, ColorSpace colorSpace, bool scalar)
    {
        auto path = std::string{reference.C_Str()};
        std::ranges::replace(path, '\\', '/');
        const auto* embedded = scene.GetEmbeddedTexture(reference.C_Str());
        const auto  file     = (source.parent_path() / path).lexically_normal();
        const auto  key      = std::tuple{embedded ? path : file.string(), colorSpace, scalar};
        if (const auto found = textures.find(key); found != textures.end())
            return found->second;

        int                  width = 0, height = 0, channels = 0;
        std::vector<uint8_t> pixels;
        if (embedded && embedded->mHeight != 0)
        {
            CHECK(
                embedded->mWidth > 0 &&
                uint64_t{embedded->mWidth} * embedded->mHeight <= std::numeric_limits<int>::max() / 4
            );
            width  = static_cast<int>(embedded->mWidth);
            height = static_cast<int>(embedded->mHeight);
            pixels.reserve(static_cast<size_t>(width) * height * 4);
            for (size_t index = 0; index < static_cast<size_t>(width) * height; ++index)
            {
                const auto& texel = embedded->pcData[index];
                pixels.insert(pixels.end(), {texel.r, texel.g, texel.b, texel.a});
            }
        }
        else
        {
            CHECK(!embedded || embedded->mWidth <= std::numeric_limits<int>::max());
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
                embedded ? stbi_load_from_memory(
                               reinterpret_cast<const stbi_uc*>(embedded->pcData),
                               static_cast<int>(embedded->mWidth),
                               &width,
                               &height,
                               &channels,
                               4
                           )
                         : stbi_load(file.string().c_str(), &width, &height, &channels, 4),
                stbi_image_free
            );
            CHECK(
                decoded && width > 0 && height > 0,
                "cannot decode texture '{}' in '{}': {}",
                path,
                source.string(),
                stbi_failure_reason()
            );
            CHECK(uint64_t{static_cast<uint32_t>(width)} * height <= std::numeric_limits<int>::max() / 4);
            pixels.assign(decoded.get(), decoded.get() + static_cast<size_t>(width) * height * 4);
        }
        // Assimp scalar maps use R. Replicate it to the channels expected by the existing PBR shaders.
        if (scalar)
            for (size_t offset = 0; offset < pixels.size(); offset += 4)
                pixels[offset + 1] = pixels[offset + 2] = pixels[offset];

        TextureAsset asset;
        asset.id =
            asset_import::nextId<TextureAsset>(embedded ? source.stem().string() + "_texture" : file.stem().string());
        asset.format     = ImageFormat::RGBA8;
        asset.colorSpace = colorSpace;
        asset.layout     = ImageLayout::Image2D;
        asset.width      = static_cast<uint32_t>(width);
        asset.height     = static_cast<uint32_t>(height);
        asset.mipLevels  = texture_mip::maxLevels(asset.width, asset.height);
        if (!embedded)
            asset.path = asset_import::sourcePathForAsset(file);
        pixels        = texture_mip::generateChain(asset, pixels);
        asset.binary  = context().assetDataManager->writeTexture(asset, pixels);
        const auto id = context().assetManager->save(std::move(asset));
        textures.emplace(key, id);
        return id;
    }

    std::optional<TextureBinding>
    texture(const aiMaterial& material, aiTextureType type, ColorSpace colorSpace, bool scalar = false)
    {
        if (material.GetTextureCount(type) == 0)
            return std::nullopt;
        CHECK(material.GetTextureCount(type) == 1, "layered textures are unsupported: {}", source.string());
        aiString                        path;
        aiTextureMapping                mapping;
        unsigned                        uvIndex = 0;
        std::array<aiTextureMapMode, 3> modes{aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        CHECK(material.GetTexture(type, 0, &path, &mapping, &uvIndex, nullptr, nullptr, modes.data()) == AI_SUCCESS);
        CHECK(
            mapping == aiTextureMapping_UV && uvIndex == 0,
            "material textures require UV channel 0: {}",
            source.string()
        );

        auto sampler = BuiltinAssets::Sampler::linearRepeat;
        if (modes != std::array{aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap})
        {
            auto [found, inserted] = samplers.try_emplace(modes);
            if (inserted)
            {
                const auto addressMode = [](aiTextureMapMode mode)
                {
                    switch (mode)
                    {
                        case aiTextureMapMode_Wrap:
                            return SamplerAsset::AddressMode::Repeat;
                        case aiTextureMapMode_Clamp:
                            return SamplerAsset::AddressMode::ClampToEdge;
                        case aiTextureMapMode_Mirror:
                            return SamplerAsset::AddressMode::MirroredRepeat;
                        case aiTextureMapMode_Decal:
                            return SamplerAsset::AddressMode::ClampToBorder;
                        default:
                            LOG_FATAL("unsupported Assimp texture addressing mode {}", int(mode));
                    }
                };
                SamplerAsset asset;
                asset.id           = asset_import::nextId<SamplerAsset>(source.stem().string());
                asset.addressModeU = addressMode(modes[0]);
                asset.addressModeV = addressMode(modes[1]);
                asset.addressModeW = addressMode(modes[2]);
                found->second      = context().assetManager->save(std::move(asset));
            }
            sampler = found->second;
        }
        return TextureBinding{importTexture(path, colorSpace, scalar), sampler};
    }

    PbrMaterialAsset material(const aiMaterial& sourceMaterial)
    {
        PbrMaterialAsset result;
        aiColor4D        baseColor{1, 1, 1, 1};
        if (sourceMaterial.Get(AI_MATKEY_BASE_COLOR, baseColor) != AI_SUCCESS)
            sourceMaterial.Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
        float opacity = 1;
        sourceMaterial.Get(AI_MATKEY_OPACITY, opacity);
        result.baseColorFactor = glm::vec4{baseColor.r, baseColor.g, baseColor.b, baseColor.a * opacity};
        result.alphaMode       = result.baseColorFactor->a < 1 ? "BLEND" : "OPAQUE";
        int doubleSided        = 0;
        sourceMaterial.Get(AI_MATKEY_TWOSIDED, doubleSided);
        result.doubleSided = doubleSided != 0;
        auto baseTexture   = texture(sourceMaterial, aiTextureType_BASE_COLOR, ColorSpace::Srgb);
        if (!baseTexture)
            baseTexture = texture(sourceMaterial, aiTextureType_DIFFUSE, ColorSpace::Srgb);
        result.baseColorTexture =
            baseTexture.value_or(TextureBinding{BuiltinAssets::Texture::white, BuiltinAssets::Sampler::linearRepeat});
        result.normalTexture    = texture(sourceMaterial, aiTextureType_NORMALS, ColorSpace::Linear);
        result.metallicTexture  = texture(sourceMaterial, aiTextureType_METALNESS, ColorSpace::Linear, true);
        result.roughnessTexture = texture(sourceMaterial, aiTextureType_DIFFUSE_ROUGHNESS, ColorSpace::Linear, true);
        result.aoTexture        = texture(sourceMaterial, aiTextureType_AMBIENT_OCCLUSION, ColorSpace::Linear, true);
        result.emissiveTexture  = texture(sourceMaterial, aiTextureType_EMISSIVE, ColorSpace::Srgb);

        float metallic = result.metallicTexture ? 1 : 0;
        sourceMaterial.Get(AI_MATKEY_METALLIC_FACTOR, metallic);
        float roughness = 1;
        if (sourceMaterial.Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) != AI_SUCCESS && !result.roughnessTexture)
        {
            float shininess = 0;
            if (sourceMaterial.Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS)
                roughness = std::sqrt(2.0f / (std::max(shininess, 0.0f) + 2.0f));
        }
        result.metallic  = std::clamp(metallic, 0.0f, 1.0f);
        result.roughness = std::clamp(roughness, 0.0f, 1.0f);
        aiColor3D emissive{0, 0, 0};
        sourceMaterial.Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
        result.emissive = glm::vec3{emissive.r, emissive.g, emissive.b};
        float strength  = 1;
        sourceMaterial.Get(AI_MATKEY_EMISSIVE_INTENSITY, strength);
        result.emissiveStrength = strength;
        return result;
    }
};
}  // namespace

AssimpImport::AssimpImport(const std::filesystem::path& path) :
    ModelImport(path), importer(std::make_unique<Assimp::Importer>())
{
    importer->SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    scene = importer->ReadFile(source.string(), kImportFlags);
    CHECK(scene && scene->mRootNode, "Assimp cannot load '{}': {}", source.string(), importer->GetErrorString());
    LOG_INFO("Assimp parsed '{}': {} meshes, {} materials", source.string(), scene->mNumMeshes, scene->mNumMaterials);
}

AssimpImport::~AssimpImport() = default;

bool AssimpImport::supports(const std::filesystem::path& source)
{
    static const Assimp::Importer kImporter;
    return kImporter.IsExtensionSupported(source.extension().string());
}

std::string AssimpImport::extensions()
{
    Assimp::Importer importer;
    aiString         extensions;
    importer.GetExtensionList(extensions);
    return extensions.C_Str();
}

void AssimpImport::visitNodes(const std::function<void(const Node&)>& visitor, float importScale) const
{
    CHECK(std::isfinite(importScale) && importScale > 0);
    uint32_t   nodeIndex = 0;
    const auto visit     = [&](this auto&& self, const aiNode& node, const glm::mat4& parent) -> void
    {
        const auto world = parent * toGlm(node.mTransformation);
        Node       imported{.index = nodeIndex++, .name = node.mName.C_Str(), .world = world};
        for (uint32_t index = 0; index < node.mNumMeshes; ++index)
        {
            const auto meshIndex = node.mMeshes[index];
            CHECK(meshIndex < scene->mNumMeshes);
            if (scene->mMeshes[meshIndex]->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)
                imported.meshes.push_back(meshIndex);
        }
        for (uint32_t index = 0; index < scene->mNumCameras; ++index)
        {
            const auto& camera = *scene->mCameras[index];
            if (camera.mName != node.mName)
                continue;
            CHECK(camera.mOrthographicWidth == 0, "editor camera requires perspective: {}", source.string());
            const float aspect = camera.mAspect > 0 ? camera.mAspect : 1.0f;
            imported.camera    = Camera{
                   .position    = glm::vec3{world * glm::vec4{toGlm(camera.mPosition), 1}},
                   .direction   = glm::normalize(glm::vec3{world * glm::vec4{toGlm(camera.mLookAt), 0}}),
                   .fieldOfView = glm::degrees(2.0f * std::atan(std::tan(camera.mHorizontalFOV) / aspect)),
                   .nearPlane   = camera.mClipPlaneNear * importScale,
                   .farPlane    = camera.mClipPlaneFar * importScale,
            };
            break;
        }
        visitor(imported);
        for (uint32_t child = 0; child < node.mNumChildren; ++child)
            self(*node.mChildren[child], world);
    };
    visit(*scene->mRootNode, glm::scale(glm::mat4{1}, glm::vec3{importScale}));
}

AssetImporter::PreparedMesh AssimpImport::readMesh(std::optional<uint32_t> meshIndex) const
{
    AssetImporter::PreparedMesh result;
    result.source = source;
    if (meshIndex)
    {
        CHECK(*meshIndex < scene->mNumMeshes);
        appendMesh(result, *scene->mMeshes[*meshIndex], glm::mat4{1});
    }
    else
        visitNodes(
            [&](const Node& node)
            {
                for (auto index : node.meshes)
                    appendMesh(result, *scene->mMeshes[index], node.world);
            }
        );
    return result;
}

std::vector<MaterialAsset::ID> AssimpImport::importMaterials()
{
    std::vector<MaterialAsset::ID> ids{BuiltinAssets::Material::white};
    MaterialImport                 build{*scene, source};
    for (uint32_t index = 0; index < scene->mNumMaterials; ++index)
    {
        auto material = build.material(*scene->mMaterials[index]);
        material.id   = asset_import::nextId<MaterialAsset>(source.stem().string());
        ids.push_back(context().assetManager->save(std::move(material)));
    }
    return ids;
}
