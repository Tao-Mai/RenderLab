#include "asset/import/AssimpImport.h"

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
#include <cctype>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <map>

namespace
{
constexpr unsigned kImportFlags = aiProcess_Triangulate | aiProcess_FindInvalidData | aiProcess_SortByPType |
    aiProcess_FlipUVs | aiProcess_TransformUVCoords | aiProcess_ValidateDataStructure;

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

void removeDegenerateTriangles(aiMesh& mesh, const std::filesystem::path& source)
{
    if (!(mesh.mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
        return;

    const auto* end = std::remove_if(
        mesh.mFaces,
        mesh.mFaces + mesh.mNumFaces,
        [&](const aiFace& face)
        {
            CHECK(face.mNumIndices == 3);
            for (uint32_t corner = 0; corner < 3; ++corner)
                CHECK(face.mIndices[corner] < mesh.mNumVertices);
            const auto  a           = toGlm(mesh.mVertices[face.mIndices[0]]);
            const auto  b           = toGlm(mesh.mVertices[face.mIndices[1]]);
            const auto  c           = toGlm(mesh.mVertices[face.mIndices[2]]);
            const auto  normal      = glm::cross(b - a, c - a);
            const float areaSquared = glm::dot(normal, normal);
            CHECK(std::isfinite(areaSquared), "invalid triangle in mesh '{}': {}", mesh.mName.C_Str(), source.string());

            return areaSquared == 0.0f;
        }
    );
    const auto faceCount = static_cast<uint32_t>(end - mesh.mFaces);
    if (faceCount != mesh.mNumFaces)
        LOG_WARNING(
            "Removed {} zero-area triangles from mesh '{}': {}",
            mesh.mNumFaces - faceCount,
            mesh.mName.C_Str(),
            source.string()
        );
    mesh.mNumFaces = faceCount;
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

    for (uint32_t faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex)
    {
        const auto& face = mesh.mFaces[faceIndex];
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
    const auto indexCount = static_cast<uint32_t>(build.geometry.indices.size()) - firstIndex;
    generateTangents(build.geometry, firstIndex, indexCount);
    asset_import::transformVertices(build.geometry, firstVertex, transform);
    if (glm::determinant(glm::mat3{transform}) < 0)
        for (uint32_t index = firstIndex; index < firstIndex + indexCount; index += 3)
            std::swap(build.geometry.indices[index + 1], build.geometry.indices[index + 2]);
    build.submeshes.push_back({firstIndex, indexCount, static_cast<int>(mesh.mMaterialIndex)});
}

struct MaterialImport
{
    const aiScene&                                                 scene;
    const std::filesystem::path&                                   source;
    ShadingModel                                                   shadingModel;
    std::map<std::pair<std::string, ColorSpace>, TextureAsset::ID> textures;
    std::map<std::array<aiTextureMapMode, 3>, SamplerAsset::ID>    samplers;

    TextureAsset::ID importTexture(const aiString& reference, ColorSpace colorSpace)
    {
        auto path = std::string{reference.C_Str()};
        std::ranges::replace(path, '\\', '/');
        const auto* embedded = scene.GetEmbeddedTexture(reference.C_Str());
        const auto  file     = (source.parent_path() / path).lexically_normal();
        const auto  key      = std::pair{embedded ? path : file.string(), colorSpace};
        if (const auto found = textures.find(key); found != textures.end())
            return found->second;

        if (!embedded)
        {
            const auto id = AssetImporter::importTexture({.source = file, .colorSpace = colorSpace});
            textures.emplace(key, id);

            return id;
        }

        int                  width = 0, height = 0, channels = 0;
        std::vector<uint8_t> pixels;
        if (embedded->mHeight != 0)
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
            CHECK(embedded->mWidth <= std::numeric_limits<int>::max());
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
                stbi_load_from_memory(
                    reinterpret_cast<const stbi_uc*>(embedded->pcData),
                    static_cast<int>(embedded->mWidth),
                    &width,
                    &height,
                    &channels,
                    4
                ),
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
        TextureAsset asset;
        asset.id         = asset_import::nextId<TextureAsset>(source.stem().string() + "_texture");
        asset.format     = ImageFormat::RGBA8;
        asset.colorSpace = colorSpace;
        asset.layout     = ImageLayout::Image2D;
        asset.width      = static_cast<uint32_t>(width);
        asset.height     = static_cast<uint32_t>(height);
        asset.mipLevels  = texture_mip::maxLevels(asset.width, asset.height);
        pixels           = texture_mip::generateChain(asset, pixels);
        asset.binary     = context().assetManager->writeTexture(asset, pixels);
        const auto id    = context().assetManager->save(std::move(asset));
        textures.emplace(key, id);
        return id;
    }

    std::optional<TextureBinding> texture(const aiMaterial& material, aiTextureType type, ColorSpace colorSpace)
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
        return TextureBinding{importTexture(path, colorSpace), sampler};
    }

    StandardMaterial material(const aiMaterial& sourceMaterial)
    {
        StandardMaterial result;
        result.shadingModel = shadingModel;
        aiColor3D diffuse{1.0f, 1.0f, 1.0f};
        sourceMaterial.Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
        float opacity = 1.0f;
        sourceMaterial.Get(AI_MATKEY_OPACITY, opacity);
        result.baseColor            = {diffuse.r, diffuse.g, diffuse.b, opacity};
        result.specularTransmission = 1.0f - opacity;
        sourceMaterial.Get(AI_MATKEY_REFRACTI, result.indexOfRefraction);

        // Falcor's default (including FBX) reads Specular RGB as packed MR parameters.
        // Only OBJ uses specular color and converts the Phong exponent to glossiness.
        aiColor3D specular{0.0f, 0.0f, 0.0f};
        sourceMaterial.Get(AI_MATKEY_COLOR_SPECULAR, specular);
        result.specularParams = {specular.r, specular.g, specular.b, 1.0f};
        if (shadingModel == ShadingModel::SpecGloss)
        {
            float shininess = 0.0f;
            sourceMaterial.Get(AI_MATKEY_SHININESS, shininess);
            CHECK(std::isfinite(shininess) && shininess >= 0.0f, "invalid Phong exponent in '{}'", source.string());
            result.specularParams.a = 1.0f - std::clamp(std::sqrt(2.0f / (shininess + 2.0f)), 0.0f, 1.0f);
        }
        result.baseColorTexture = texture(sourceMaterial, aiTextureType_DIFFUSE, ColorSpace::Srgb);
        result.specularTexture  = texture(
            sourceMaterial,
            aiTextureType_SPECULAR,
            shadingModel == ShadingModel::MetalRough ? ColorSpace::Linear : ColorSpace::Srgb
        );
        result.normalTexture = texture(sourceMaterial, aiTextureType_NORMALS, ColorSpace::Linear);
        if (shadingModel == ShadingModel::SpecGloss)
        {
            if (const auto bump = texture(sourceMaterial, aiTextureType_HEIGHT, ColorSpace::Linear))
                result.normalTexture = bump;
            if (const auto displacement = texture(sourceMaterial, aiTextureType_DISPLACEMENT, ColorSpace::Linear))
                result.normalTexture = displacement;
        }

        aiColor3D emissive{0.0f, 0.0f, 0.0f};
        sourceMaterial.Get(AI_MATKEY_COLOR_EMISSIVE, emissive);
        result.emissiveColor = {emissive.r, emissive.g, emissive.b};
        sourceMaterial.Get(AI_MATKEY_EMISSIVE_INTENSITY, result.emissiveFactor);
        result.emissiveTexture = texture(sourceMaterial, aiTextureType_EMISSIVE, ColorSpace::Srgb);
        result.alphaMode =
            result.baseColorTexture || opacity < result.alphaCutoff ? AlphaMode::Mask : AlphaMode::Opaque;

        int doubleSided = 0;
        sourceMaterial.Get(AI_MATKEY_TWOSIDED, doubleSided);
        result.doubleSided = doubleSided != 0;
        aiString materialName;
        sourceMaterial.Get(AI_MATKEY_NAME, materialName);
        std::string name{materialName.C_Str()};
        std::ranges::transform(name, name.begin(), [](unsigned char character) { return std::tolower(character); });
        for (auto start = name.find('.'); start != std::string::npos;)
        {
            const auto next = name.find('.', start + 1);
            if (name.substr(start + 1, next - start - 1) == "doublesided")
                result.doubleSided = true;
            start = next;
        }

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
    for (uint32_t index = 0; index < scene->mNumMeshes; ++index)
        removeDegenerateTriangles(*scene->mMeshes[index], source);

    // Generate missing normals after invalid-data removal and zero-area face filtering.
    scene = importer->ApplyPostProcessing(aiProcess_GenNormals | aiProcess_JoinIdenticalVertices);
    CHECK(scene, "Assimp cannot generate normals for '{}': {}", source.string(), importer->GetErrorString());

    LOG_INFO("Assimp parsed '{}': {} meshes, {} materials", source.string(), scene->mNumMeshes, scene->mNumMaterials);
}

AssimpImport::~AssimpImport() = default;

bool AssimpImport::supports(const std::filesystem::path& source)
{
    static const Assimp::Importer kImporter;
    return kImporter.IsExtensionSupported(source.extension().string());
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
            if ((scene->mMeshes[meshIndex]->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) &&
                scene->mMeshes[meshIndex]->mNumFaces > 0)
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
        for (uint32_t index = 0; index < scene->mNumLights; ++index)
        {
            const auto& light = *scene->mLights[index];
            if (light.mName != node.mName)
                continue;
            if (light.mType != aiLightSource_DIRECTIONAL && light.mType != aiLightSource_POINT &&
                light.mType != aiLightSource_SPOT)
            {
                LOG_WARNING(
                    "Unsupported Assimp light '{}' of type {} in '{}'",
                    light.mName.C_Str(),
                    static_cast<int>(light.mType),
                    source.string()
                );
                continue;
            }

            // Assimp point lights may omit their direction and up vector, as in Falcor's importer.
            CHECK(
                light.mType != aiLightSource_DIRECTIONAL || light.mDirection.SquareLength() > 0.0f,
                "invalid directional light '{}' in '{}'",
                light.mName.C_Str(),
                source.string()
            );
            const auto direction =
                light.mDirection.SquareLength() == 0.0f ? glm::vec3{0, 0, -1} : glm::normalize(toGlm(light.mDirection));
            const auto up    = light.mUp.SquareLength() == 0.0f ? glm::vec3{0, 1, 0} : glm::normalize(toGlm(light.mUp));
            const auto right = glm::cross(direction, up);
            CHECK(glm::dot(right, right) > 0.0f, "invalid light frame in '{}'", source.string());
            const glm::vec3 color{light.mColorDiffuse.r, light.mColorDiffuse.g, light.mColorDiffuse.b};
            const auto      position = toGlm(light.mPosition);
            for (glm::length_t channel = 0; channel < 3; ++channel)
                CHECK(
                    std::isfinite(color[channel]) && color[channel] >= 0.0f && std::isfinite(position[channel]),
                    "invalid light '{}' in '{}'",
                    light.mName.C_Str(),
                    source.string()
                );
            const float intensity = std::max({color.r, color.g, color.b});
            CHECK(std::isfinite(intensity) && intensity >= 0.0f, "invalid light intensity in '{}'", source.string());
            const bool spot = light.mType == aiLightSource_SPOT;
            if (spot)
                CHECK(
                    std::isfinite(light.mAngleInnerCone) && std::isfinite(light.mAngleOuterCone) &&
                        light.mAngleInnerCone >= 0.0f && light.mAngleInnerCone <= light.mAngleOuterCone &&
                        light.mAngleOuterCone <= glm::half_pi<float>(),
                    "invalid spot cone in '{}'",
                    source.string()
                );

            imported.light = Light{
                .type = light.mType == aiLightSource_DIRECTIONAL ? Light::Type::Directional : Light::Type::Point,
                .localTransform =
                    glm::mat4{glm::vec4{right, 0}, glm::vec4{up, 0}, glm::vec4{-direction, 0}, glm::vec4{position, 1}},
                .color         = intensity > 0.0f ? color / intensity : color,
                .intensity     = intensity,
                .openingAngle  = spot ? light.mAngleOuterCone : glm::pi<float>(),
                .penumbraAngle = spot ? light.mAngleOuterCone - light.mAngleInnerCone : 0.0f,
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

std::vector<StandardMaterial::ID> AssimpImport::importMaterials()
{
    std::vector<StandardMaterial::ID> ids{BuiltinAssets::Material::white};
    std::string                       extension = source.extension().string();
    std::ranges::transform(
        extension,
        extension.begin(),
        [](unsigned char character) { return std::tolower(character); }
    );
    MaterialImport build{*scene, source, extension == ".obj" ? ShadingModel::SpecGloss : ShadingModel::MetalRough};
    for (uint32_t index = 0; index < scene->mNumMaterials; ++index)
    {
        auto material = build.material(*scene->mMaterials[index]);
        material.id   = asset_import::nextId<StandardMaterial>(source.stem().string());
        ids.push_back(context().assetManager->save(std::move(material)));
    }
    return ids;
}
