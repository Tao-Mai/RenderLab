#include "asset/asset_manager.h"

#include "asset/json_io.h"
#include "core/config_manager.h"
#include "core/context.h"
#include "logger.h"
#include "scene/light.h"

#include <algorithm>
#include <utility>

#include <glm/geometric.hpp>

namespace
{
void validateLight(Light& light)
{
    CHECK(light.intensity >= 0.0f, "light intensity cannot be negative: {}", light.name);
    CHECK((light.type != Light::Type::Point && light.type != Light::Type::Spot) ||
          light.range > 0.0f,
        "light range must be greater than zero: {}", light.name);
    if (light.type != Light::Type::Point)
    {
        CHECK(glm::dot(light.direction, light.direction) >= 0.000001f,
            "light direction cannot be zero: {}", light.name);
        light.direction = glm::normalize(light.direction);
    }
    if (light.type == Light::Type::Spot)
    {
        light.cosInner = std::clamp(light.cosInner, -1.0f, 1.0f);
        light.cosOuter = std::clamp(light.cosOuter, -1.0f, 1.0f);
        CHECK(light.cosInner >= light.cosOuter,
            "spot light inner angle must not exceed outer angle: {}", light.name);
    }
    CHECK(light.type != Light::Type::RectArea ||
          (light.areaSize.x > 0.0f && light.areaSize.y > 0.0f),
        "area light size must be greater than zero: {}", light.name);
}

void validateDesc(MeshDesc& desc, const std::filesystem::path& file)
{
    CHECK(!desc.source.kind.empty(),
        "mesh descriptor missing source.kind: {}", file.string());
}

void validateDesc(MaterialDesc& desc, const std::filesystem::path& file)
{
    CHECK(desc.baseColorTexture != kInvalidAssetId,
        "material requires baseColorTexture: {}", file.string());
}

void validateDesc(TextureDesc& desc, const std::filesystem::path& file)
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
}

void validateDesc(ShaderDesc& desc, const std::filesystem::path& file)
{
    CHECK(!desc.binary.empty(), "invalid shader descriptor: {}", file.string());
}

void validateDesc(SceneDesc& scene, const std::filesystem::path&)
{
    CHECK(scene.asset().type == AssetType::Scene,
        "scene descriptor type mismatch: {}", scene.asset().id);
    CHECK(scene.version == 2, "unsupported scene description version");
    CHECK(scene.camera.movementSpeed > 0.0f,
        "camera movement speed must be greater than zero");
    CHECK(scene.camera.sprintMultiplier >= 1.0f,
        "camera sprint multiplier must be at least one");
    for (Light& light : scene.lights)
    {
        validateLight(light);
    }
}

void prepareDesc(MeshDesc& desc)
{
    CHECK(desc.asset().id != kInvalidAssetId, "mesh id is required");
    CHECK(!desc.geometry.empty(), "mesh geometry path is required");
    desc.asset().type = AssetType::Mesh;
}

void prepareDesc(MaterialDesc& desc)
{
    if (desc.asset().id == kInvalidAssetId)
    {
        // id filled by save()
    }
    desc.asset().type = AssetType::Material;
    if (desc.baseColorTexture == kInvalidAssetId)
    {
        desc.baseColorTexture = BuiltinId::whiteTexture;
    }
}

void prepareDesc(TextureDesc& desc)
{
    desc.asset().type = AssetType::Texture;
}

void prepareDesc(ShaderDesc& desc)
{
    desc.asset().type = AssetType::Shader;
}

void prepareDesc(SceneDesc& desc)
{
    desc.asset().type = AssetType::Scene;
}

[[nodiscard]] std::string nameFileStem(std::string_view name)
{
    CHECK(!name.empty(), "asset name cannot be empty");
    std::string stem{name};
    for (char& character : stem)
    {
        if (character == ':' || character == '/' || character == '\\' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|')
        {
            character = '_';
        }
    }
    return stem;
}

template <class T>
void loadDescDirectory(
    const ConfigManager& config,
    std::unordered_map<AssetId, T>& out,
    std::unordered_map<AssetId, std::filesystem::path>& descFiles)
{
    const std::filesystem::path& directory = config.descDir(assetTypeOf<T>);
    if (!std::filesystem::exists(directory))
    {
        return;
    }
    constexpr AssetType expectedType = assetTypeOf<T>;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        T desc = asset_json::load<T>(entry.path());
        CHECK(desc.asset().id != kInvalidAssetId,
            "descriptor has invalid id: {}", entry.path().string());
        CHECK(!desc.asset().name.empty(),
            "descriptor missing name: {}", entry.path().string());
        CHECK(desc.asset().type == expectedType,
            "descriptor type mismatch in {}: expected {}, got {}",
            entry.path().string(),
            assetTypeDir(expectedType),
            assetTypeDir(desc.asset().type));
        validateDesc(desc, entry.path());
        CHECK(out.emplace(desc.asset().id, desc).second,
            "duplicate asset ID: {}", desc.asset().id);
        descFiles.insert_or_assign(desc.asset().id, entry.path());
    }
}
}

void AssetManager::init()
{
    if (ready)
    {
        return;
    }
    CHECK(context().config != nullptr, "ConfigManager must exist before AssetManager");
    root = std::filesystem::absolute(context().config->assetRoot()).lexically_normal();
    loadAll();
    ready = true;
}

void AssetManager::shutdown() noexcept
{
    clear();
    root.clear();
    ready = false;
}

void AssetManager::storeDescFile(AssetId id, const std::filesystem::path& file)
{
    descFiles.insert_or_assign(id, file);
}

std::filesystem::path AssetManager::descFile(AssetType type, std::string_view name) const
{
    return context().config->descDir(type) / (nameFileStem(name) + ".json");
}

AssetId AssetManager::allocateId()
{
    AssetId next = kFirstUserAssetId;
    auto consider = [&](const auto& map)
    {
        for (const auto& [id, _] : map)
        {
            if (id >= kFirstUserAssetId)
            {
                next = std::max(next, static_cast<AssetId>(id + 1));
            }
        }
    };
    std::apply([&](const auto&... maps) { (consider(maps), ...); }, descs);
    return next;
}

void AssetManager::loadAll()
{
    clear();
    const ConfigManager& config = *context().config;
    std::apply(
        [&](auto&... maps) { (loadDescDirectory(config, maps, descFiles), ...); },
        descs);
}

void AssetManager::reload()
{
    loadAll();
}

std::filesystem::path AssetManager::path(const std::filesystem::path& relative) const
{
    CHECK(!relative.is_absolute(), "asset path must be relative: {}", relative.string());
    return root / relative;
}

template <class T>
AssetId AssetManager::save(T desc)
{
    if (desc.asset().id == kInvalidAssetId)
    {
        desc.asset().id = allocateId();
    }
    if (desc.asset().name.empty())
    {
        desc.asset().name = std::string(assetTypeDir(assetTypeOf<T>)) + "_" +
            std::to_string(desc.asset().id);
    }
    prepareDesc(desc);
    validateDesc(desc, {});
    const auto file = descFile(assetTypeOf<T>, desc.asset().name);
    asset_json::save(file, desc);
    storeDescFile(desc.asset().id, file);
    const AssetId id = desc.asset().id;
    descMap<T>().insert_or_assign(id, std::move(desc));
    return id;
}

template AssetId AssetManager::save(MeshDesc desc);
template AssetId AssetManager::save(MaterialDesc desc);
template AssetId AssetManager::save(TextureDesc desc);
template AssetId AssetManager::save(ShaderDesc desc);
template AssetId AssetManager::save(SceneDesc desc);

void AssetManager::clear()
{
    std::apply([](auto&... maps) { (maps.clear(), ...); }, descs);
    descFiles.clear();
}
