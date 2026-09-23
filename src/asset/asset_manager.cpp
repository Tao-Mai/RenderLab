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
    CHECK(!desc.geometry.empty(), "mesh geometry path is required: {}", file.string());
}

void validateDesc(MaterialDesc& desc, const std::filesystem::path& file)
{
    if (desc.baseColorTexture == kInvalidAssetId)
    {
        desc.baseColorTexture = BuiltinId::whiteTexture;
    }
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
    CHECK(scene.version == 3, "unsupported scene description version");
    CHECK(scene.camera.movementSpeed > 0.0f,
        "camera movement speed must be greater than zero");
    CHECK(scene.camera.sprintMultiplier >= 1.0f,
        "camera sprint multiplier must be at least one");
    for (Light& light : scene.lights)
    {
        validateLight(light);
    }
}

template <class T>
void loadDescDirectory(
    const std::filesystem::path& directory, std::unordered_map<AssetId, T>& out)
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
        CHECK(!desc.id.empty(), "descriptor has empty id: {}", entry.path().string());
        validateDesc(desc, entry.path());
        CHECK(out.emplace(desc.id, desc).second,
            "duplicate asset ID: {}", desc.id);
    }
}
}

void AssetManager::init()
{
    if (inited)
    {
        return;
    }
    CHECK(context().config != nullptr, "ConfigManager must exist before AssetManager");
    root = std::filesystem::absolute(context().config->assetRoot()).lexically_normal();
    loadAll();
    inited = true;
}

void AssetManager::shutdown() noexcept
{
    clear();
    root.clear();
    inited = false;
}

void AssetManager::loadAll()
{
    clear();
    const ConfigManager& config = *context().config;
    loadDescDirectory(config.descDir(AssetType::mesh), descMap<MeshDesc>());
    loadDescDirectory(config.descDir(AssetType::material), descMap<MaterialDesc>());
    loadDescDirectory(config.descDir(AssetType::texture), descMap<TextureDesc>());
    loadDescDirectory(config.descDir(AssetType::shader), descMap<ShaderDesc>());
    loadDescDirectory(config.descDir(AssetType::scene), descMap<SceneDesc>());
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
    CHECK(!desc.id.empty(), "descriptor has empty id");
    validateDesc(desc, {});
    const auto file =
        context().config->descDir(assetTypeOf<T>) / (desc.id + ".json");
    asset_json::save(file, desc);
    const AssetId id = desc.id;
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
}
