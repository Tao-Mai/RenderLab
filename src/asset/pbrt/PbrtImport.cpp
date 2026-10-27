#include "asset/AssetDataManager.h"
#include "asset/AssetImporter.h"
#include "asset/AssetManager.h"
#include "asset/import/ImportId.h"
#include "asset/pbrt/PbrtFrontend.h"
#include "asset/pbrt/PbrtMaterial.h"
#include "core/Context.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <fstream>
#include <set>
#include <unordered_map>

namespace
{
constexpr size_t kInlineArrayLimit = 64 * 1024;
constexpr size_t kArrayAlignment   = 8;
enum class EntityKind
{
    Camera,
    Film,
    Shape,
    Material,
    Texture,
    Light,
    AreaLight,
    Medium,
    Settings
};

template <class Visitor>
void visitEntities(pbrt_import::ReadResult& result, Visitor&& visitor)
{
    auto& scene = result.scene;
    visitor(scene.camera, EntityKind::Camera, PbrtTextureType::Spectrum);
    visitor(scene.film, EntityKind::Film, PbrtTextureType::Spectrum);
    for (auto* entity : {&scene.sampler, &scene.integrator, &scene.pixelFilter, &scene.accelerator})
        visitor(*entity, EntityKind::Settings, PbrtTextureType::Spectrum);
    for (auto& shape : scene.shapes)
        visitor(shape.entity, EntityKind::Shape, PbrtTextureType::Spectrum);
    for (auto& light : scene.lights)
        visitor(light.entity, EntityKind::Light, PbrtTextureType::Spectrum);
    for (auto& light : scene.areaLights)
        visitor(light, EntityKind::AreaLight, PbrtTextureType::Spectrum);
    for (auto& medium : scene.media)
        visitor(medium.entity, EntityKind::Medium, PbrtTextureType::Spectrum);
    for (auto& material : result.materials)
        visitor(material.entity, EntityKind::Material, PbrtTextureType::Spectrum);
    for (auto& texture : result.textures)
        visitor(texture.entity, EntityKind::Texture, texture.valueType);
}

std::vector<uint8_t> readSource(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CHECK(input, "cannot read PBRT dependency: {}", path.string());
    const auto size = input.tellg();
    CHECK(size >= 0, "cannot determine PBRT dependency size: {}", path.string());
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    CHECK(input, "cannot read complete PBRT dependency: {}", path.string());
    return bytes;
}

class ImportSession
{
public:
    ImportSession(pbrt_import::ReadResult& result, PbrtSceneAsset::ID id, std::filesystem::path source) :
        result(result), sourceRoot(source.parent_path()), package(std::filesystem::path("binary/pbrt") / id.value)
    {
        result.scene.id = std::move(id);
        for (size_t index = 0; index < result.materials.size(); ++index)
        {
            MaterialAsset::ID materialId = result.scene.id.value + "_material" + std::to_string(index);
            CHECK(!context().assetManager->find(materialId), "PBRT material ID already exists: {}", materialId);
            result.scene.materials.push_back(materialId);
            if (!result.materials[index].name.empty())
                namedMaterials.emplace(result.materials[index].name, materialId);
        }
        for (size_t index = 0; index < result.textures.size(); ++index)
        {
            auto& texture = result.textures[index];
            texture.id    = result.scene.id.value + "_texture" + std::to_string(index);
            CHECK(!context().assetManager->find(texture.id), "PBRT texture ID already exists: {}", texture.id);
            result.scene.textures.push_back(texture.id);
            auto& table = texture.valueType == PbrtTextureType::Float ? floatTextures : spectrumTextures;
            CHECK(table.emplace(texture.name, texture.id).second, "duplicate PBRT texture: {}", texture.name);
        }
        DCHECK(result.scene.shapes.size() == result.shapeMaterialIndices.size());
        for (size_t index = 0; index < result.scene.shapes.size(); ++index)
            if (const auto materialIndex = result.shapeMaterialIndices[index])
            {
                DCHECK(*materialIndex < result.scene.materials.size());
                result.scene.shapes[index].material = result.scene.materials[*materialIndex];
            }
    }

    void prepare(const std::filesystem::path& source)
    {
        for (const auto& file : result.sourceFiles)
            addFile(file.string(), PbrtFileKind::Source);
        result.scene.source = addFile(source.string(), PbrtFileKind::Source).path;
        visitEntities(
            result,
            [&](PbrtEntity& entity, EntityKind kind, PbrtTextureType outputType)
            {
                for (auto& parameter : entity.parameters)
                {
                    auto* strings = std::get_if<PbrtStringValues>(&parameter.value);
                    if (!strings)
                        continue;
                    for (uint32_t index = 0; index < strings->values.size(); ++index)
                    {
                        const auto& value = strings->values[index];
                        if (parameter.type == "texture")
                        {
                            auto texture = resolveTexture(entity, parameter, outputType, kind, value);
                            parameter.references.push_back({index, PbrtTextureReference{texture}});
                        }
                        else if (kind == EntityKind::Material && entity.type == "mix" && parameter.name == "materials")
                        {
                            const auto normalized = pbrt_import::normalizeResourceName(value);
                            CHECK(
                                normalized,
                                "invalid UTF-8 material name at {}:{}",
                                parameter.location.file.string(),
                                parameter.location.line
                            );
                            auto found = namedMaterials.find(*normalized);
                            CHECK(
                                found != namedMaterials.end(),
                                "undefined PBRT material '{}' at {}:{}",
                                value,
                                parameter.location.file.string(),
                                parameter.location.line
                            );
                            parameter.references.push_back({index, PbrtMaterialReference{found->second}});
                        }
                        else if (parameter.type == "spectrum" && !pbrt_import::isNamedSpectrum(value))
                            parameter.references.push_back(
                                {index, addFile(value, PbrtFileKind::Spectrum, parameter.location)}
                            );
                        else if (auto role = fileRole(entity, kind, parameter.name, value); role && !value.empty())
                            parameter.references.push_back({index, addFile(value, *role, parameter.location)});
                    }
                }
            }
        );
        for (auto& option : result.scene.options)
            if (normalizedOptionName(option.name) == "msereferenceimage")
            {
                option.inputFile = addFile(
                    option.value.substr(1, option.value.size() - 2),
                    PbrtFileKind::ReferenceImage,
                    option.location
                );
            }
        validateGraphs();
        visitEntities(
            result,
            [&](PbrtEntity& entity, EntityKind, PbrtTextureType)
            {
                relocate(entity.location);
                for (auto& parameter : entity.parameters)
                {
                    relocate(parameter.location);
                    std::optional<PbrtArrayReference> reference;
                    std::visit(
                        [&]<class Values>(Values& values)
                        {
                            if constexpr (std::same_as<Values, PbrtFloatValues> ||
                                          std::same_as<Values, PbrtIntegerValues> ||
                                          std::same_as<Values, PbrtBoolValues>)
                            {
                                using Scalar    = typename decltype(values.values)::value_type;
                                const auto size = values.values.size() * sizeof(Scalar);
                                if (size <= kInlineArrayLimit)
                                    return;
                                const auto offset = (arrayBytes.size() + kArrayAlignment - 1) & ~(kArrayAlignment - 1);
                                arrayBytes.resize(offset + size);
                                std::memcpy(arrayBytes.data() + offset, values.values.data(), size);
                                const auto type = std::same_as<Values, PbrtFloatValues> ? PbrtArrayType::Float64
                                    : std::same_as<Values, PbrtIntegerValues>           ? PbrtArrayType::Int32
                                                                                        : PbrtArrayType::Bool8;
                                reference =
                                    PbrtArrayReference{package / "arrays.bin", offset, values.values.size(), type};
                            }
                        },
                        parameter.value
                    );
                    if (reference)
                        parameter.value = std::move(*reference);
                }
            }
        );
        for (auto& object : result.scene.objects)
            relocate(object.location);
        for (auto& instance : result.scene.instances)
            relocate(instance.location);
        for (auto& option : result.scene.options)
            relocate(option.location);
    }

    void save()
    {
        static_assert(std::endian::native == std::endian::little);
        auto& data = *context().assetDataManager;
        for (const auto& pending : pendingFiles)
        {
            const auto bytes = readSource(pending.first);
            CHECK(
                bytes.size() == pending.second.size,
                "PBRT dependency changed during import: {}",
                pending.first.string()
            );
            data.writeBlob(pending.second.path, bytes);
        }
        if (!arrayBytes.empty())
            data.writeBlob(package / "arrays.bin", arrayBytes);
        auto& assets = *context().assetManager;
        for (size_t index = 0; index < result.materials.size(); ++index)
        {
            PbrtMaterial material;
            material.id     = result.scene.materials[index];
            material.name   = std::move(result.materials[index].name);
            material.entity = std::move(result.materials[index].entity);
            assets.save(std::move(material));
        }
        for (auto& texture : result.textures)
            assets.save(std::move(texture));
        assets.save(std::move(result.scene));
    }

private:
    pbrt_import::ReadResult&                                         result;
    std::filesystem::path                                            sourceRoot;
    std::filesystem::path                                            package;
    std::unordered_map<std::string, MaterialAsset::ID>               namedMaterials;
    std::unordered_map<std::string, PbrtTextureAsset::ID>            floatTextures, spectrumTextures;
    std::map<std::filesystem::path, PbrtFileReference>               files;
    std::vector<std::pair<std::filesystem::path, PbrtFileReference>> pendingFiles;
    std::vector<uint8_t>                                             arrayBytes;

    PbrtFileReference addFile(const std::string& name, PbrtFileKind kind, const PbrtSourceLocation& location = {})
    {
        std::error_code error;
        auto            file = std::filesystem::path(name);
        if (file.is_relative())
            file = sourceRoot / file;
        file = std::filesystem::canonical(file, error);
        CHECK(
            !error,
            "cannot resolve PBRT dependency '{}' at {}:{}: {}",
            name,
            location.file.string(),
            location.line,
            error.message()
        );
        if (auto found = files.find(file); found != files.end())
        {
            auto reference         = found->second;
            reference.originalName = name;
            reference.kind         = kind;
            return reference;
        }
        const auto size = std::filesystem::file_size(file, error);
        CHECK(!error, "cannot inspect PBRT dependency '{}': {}", file.string(), error.message());
        PbrtFileReference reference{
            name,
            package / "files" / (std::to_string(files.size()) + "-" + file.filename().string()),
            kind,
            size
        };
        files.emplace(file, reference);
        pendingFiles.emplace_back(file, reference);
        result.scene.files.push_back(reference);
        return reference;
    }

    void relocate(PbrtSourceLocation& location)
    {
        if (!location.file.empty())
            location.file = addFile(location.file.string(), PbrtFileKind::Source).path;
    }

    static std::optional<PbrtFileKind>
    fileRole(const PbrtEntity& entity, EntityKind kind, const std::string& name, const std::string& value)
    {
        if (kind == EntityKind::Material && name == "normalmap")
            return PbrtFileKind::Image;
        if (kind == EntityKind::Camera && name == "lensfile")
            return PbrtFileKind::Lens;
        if (kind == EntityKind::Camera && name == "aperture" && value != "gaussian" && value != "square" &&
            value != "pentagon" && value != "star")
            return PbrtFileKind::Image;
        if (name != "filename")
            return {};
        if (kind == EntityKind::Shape && entity.type == "plymesh")
            return PbrtFileKind::Geometry;
        if (kind == EntityKind::Material && entity.type == "measured")
            return PbrtFileKind::MeasuredBrdf;
        if (kind == EntityKind::Medium && entity.type == "nanovdb")
            return PbrtFileKind::Volume;
        if (kind == EntityKind::Texture && entity.type == "ptex")
            return PbrtFileKind::Ptex;
        if (kind == EntityKind::Texture && entity.type == "imagemap")
            return PbrtFileKind::Image;
        if (kind == EntityKind::Light &&
            (entity.type == "infinite" || entity.type == "goniometric" || entity.type == "projection"))
            return PbrtFileKind::Image;
        if (kind == EntityKind::AreaLight && entity.type == "diffuse")
            return PbrtFileKind::Image;
        return {};
    }

    PbrtTextureAsset::ID resolveTexture(
        const PbrtEntity&    entity,
        const PbrtParameter& parameter,
        PbrtTextureType      outputType,
        EntityKind           kind,
        const std::string&   name
    )
    {
        const auto& slot = parameter.name;
        // Slot types follow the Create() functions at the pinned PBRT revision.
        static const std::set<std::string> kScalarMaterialSlots{
            "amount",
            "roughness",
            "uroughness",
            "vroughness",
            "displacement",
            "g",
            "thickness",
            "interface.roughness",
            "interface.uroughness",
            "interface.vroughness",
            "conductor.roughness",
            "conductor.uroughness",
            "conductor.vroughness",
            "eumelanin",
            "pheomelanin",
            "beta_m",
            "beta_n",
            "alpha"
        };
        bool scalarSlot = kScalarMaterialSlots.contains(slot) || (entity.type == "hair" && slot == "eta");
        if (kind == EntityKind::Texture)
            scalarSlot = slot == "amount" || slot == "scale" || outputType == PbrtTextureType::Float;
        if (kind == EntityKind::Shape)
            scalarSlot = slot == "alpha";
        const auto normalized = pbrt_import::normalizeResourceName(name);
        CHECK(
            normalized,
            "invalid UTF-8 texture name at {}:{}",
            parameter.location.file.string(),
            parameter.location.line
        );
        const auto& table = scalarSlot ? floatTextures : spectrumTextures;
        const auto  found = table.find(*normalized);
        CHECK(
            found != table.end(),
            "undefined PBRT texture '{}' ({} slot '{}') at {}:{}",
            name,
            scalarSlot ? "float" : "spectrum",
            slot,
            parameter.location.file.string(),
            parameter.location.line
        );
        return found->second;
    }

    static std::string normalizedOptionName(const std::string& name)
    {
        std::string result;
        for (unsigned char character : name)
            if (character != '_' && character != '-')
                result += static_cast<char>(std::tolower(character));
        return result;
    }

    void validateGraphs()
    {
        std::unordered_map<std::string, const PbrtEntity*> nodes;
        for (size_t index = 0; index < result.materials.size(); ++index)
            nodes.emplace(result.scene.materials[index].value, &result.materials[index].entity);
        for (const auto& texture : result.textures)
            nodes.emplace(texture.id.value, &texture.entity);
        std::unordered_map<std::string, uint8_t> states;
        const auto                               visit = [&](auto&& self, const std::string& id) -> void
        {
            auto& state = states[id];
            CHECK(state != 1, "cyclic PBRT resource reference: {}", id);
            if (state == 2)
                return;
            state              = 1;
            const auto& entity = *nodes.at(id);
            for (const auto& parameter : entity.parameters)
                for (const auto& reference : parameter.references)
                    std::visit(
                        [&]<class Reference>(const Reference& target)
                        {
                            if constexpr (!std::same_as<Reference, PbrtFileReference>)
                                self(self, target.id.value);
                        },
                        reference.target
                    );
            state = 2;
        };
        for (const auto& [id, entity] : nodes)
            visit(visit, id);
    }
};
}  // namespace

PbrtSceneAsset::ID AssetImporter::importPbrtScene(const ImportPbrtSceneSetting& setting)
{
    DCHECK(context().assetManager && context().assetDataManager);
    CHECK(!setting.source.empty());
    std::error_code error;
    const auto      source = std::filesystem::absolute(setting.source, error).lexically_normal();
    CHECK(!error, "cannot resolve PBRT source '{}': {}", setting.source.string(), error.message());
    auto id     = asset_import::nextId<PbrtSceneAsset>(setting.id.empty() ? source.stem().string() : setting.id.value);
    auto result = pbrt_import::readScene(
        source,
        [](bool fatal, const PbrtSourceLocation& location, const std::string& message)
        {
            CHECK(!fatal, "PBRT {}:{}:{}: {}", location.file.string(), location.line, location.column, message);
            LOG_WARNING("PBRT {}:{}: {}", location.file.string(), location.line, message);
        }
    );
    ImportSession session(result, id, source);
    session.prepare(source);
    session.save();
    LOG_INFO("Imported PBRT scene '{}'", id);
    return id;
}
