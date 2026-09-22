#include "scene/scene_manager.h"

#include "asset/json_io.h"
#include "logger.h"

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

void validateScene(SceneDesc& scene)
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
}

SceneManager::SceneManager(std::filesystem::path assetRoot) :
    root(std::filesystem::absolute(std::move(assetRoot)).lexically_normal())
{
    loadAll();
}

std::filesystem::path SceneManager::descFile(const AssetId& id) const
{
    if (const auto found = descFiles.find(id); found != descFiles.end())
    {
        return found->second;
    }
    return root / "descs" / "scene" / (idFileStem(id) + ".json");
}

void SceneManager::loadAll()
{
    clear();
    const auto directory = root / "descs" / "scene";
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
        SceneDesc scene = asset_json::load<SceneDesc>(entry.path());
        validateScene(scene);
        CHECK(scenes.emplace(scene.asset().id, scene).second,
            "duplicate scene ID: {}", scene.asset().id);
        descFiles.insert_or_assign(scene.asset().id, entry.path());
    }
}

void SceneManager::reload()
{
    loadAll();
}

const SceneDesc& SceneManager::load(const AssetId& id) const
{
    const auto found = scenes.find(id);
    CHECK(found != scenes.end(), "scene descriptor not loaded: {}", id);
    return found->second;
}

void SceneManager::clear() noexcept
{
    scenes.clear();
    descFiles.clear();
}

void SceneManager::save(const SceneDesc& scene)
{
    SceneDesc validated = scene;
    validateScene(validated);
    const auto file = descFile(validated.asset().id);
    asset_json::save(file, validated);
    descFiles.insert_or_assign(validated.asset().id, file);
    scenes.insert_or_assign(validated.asset().id, std::move(validated));
}
