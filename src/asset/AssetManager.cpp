#include "asset/AssetManager.h"

namespace
{
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
    loadAll();
    inited = true;
}

void AssetManager::shutdown() noexcept
{
    clear();
    inited = false;
}

void AssetManager::loadAll()
{
    clear();
    AssetTypes::forEach(
        [&]<class Entry>
        {
            using T = typename Entry::type;
            loadDescDirectory(
                descriptorRoot() / Entry::dir, descMap<T>());
        });
}

std::filesystem::path AssetManager::descriptorRoot() const
{
    return context().config->paths().assets / "assets";
}

std::filesystem::path AssetManager::path(const std::filesystem::path& relative) const
{
    CHECK(!relative.is_absolute(), "asset path must be relative: {}", relative.string());
    return context().config->paths().assets / relative;
}

void AssetManager::clear()
{
    std::apply([](auto&... maps) { (maps.clear(), ...); }, descs);
}
