#include "render/plugins/Plugin.h"

#include "core/RuntimeDirectory.h"
#include <fstream>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
std::filesystem::path absolutePluginPath(const std::filesystem::path& path)
{
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    CHECK(!error, "cannot resolve plugin path '{}': {}", path.string(), error.message());
    return absolute.lexically_normal();
}

std::shared_ptr<void> loadLibrary(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto module =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    CHECK(module, "cannot load render plugin '{}': error {}", path.string(), GetLastError());
    return std::shared_ptr<void>{module, [](void* handle) { FreeLibrary(static_cast<HMODULE>(handle)); }};
#else
    const auto module = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    CHECK(module, "cannot load render plugin '{}': {}", path.string(), dlerror());
    return std::shared_ptr<void>{module, [](void* handle) { dlclose(handle); }};
#endif
}

template <class Function>
Function pluginSymbol(const std::shared_ptr<void>& library, const std::filesystem::path& path, const char* name)
{
#ifdef _WIN32
    const auto address = GetProcAddress(static_cast<HMODULE>(library.get()), name);
#else
    const auto address = dlsym(library.get(), name);
#endif
    CHECK(address, "render plugin '{}' does not export '{}'", path.string(), name);
    return reinterpret_cast<Function>(address);
}
}  // namespace

PluginManager& PluginManager::instance()
{
    static PluginManager kManager;
    return kManager;
}

bool PluginManager::loadPluginByName(std::string_view name)
{
    std::filesystem::path file{name};
    CHECK(!file.empty() && file == file.filename(), "invalid plugin name '{}'", name);
    file += RENDERLAB_PLUGIN_SUFFIX;
    return loadPlugin(runtimeDirectory() / "plugins" / file);
}

bool PluginManager::loadPlugin(const std::filesystem::path& path)
{
    const auto file = absolutePluginPath(path);
    std::lock_guard lock(mLibrariesMutex);
    if (mLibraries.contains(file))
        return false;

    const auto library = loadLibrary(file);
    const auto abi = pluginSymbol<const char* (*)()>(library, file, "renderLabPluginAbi");
    const auto* tag = abi();
    CHECK(
        tag && std::string_view{tag} == RENDERLAB_PLUGIN_ABI,
        "render plugin '{}' uses a different ABI",
        file.string()
    );

    PluginRegistry registry{*this, library};
    pluginSymbol<void (*)(PluginRegistry&)>(library, file, "registerPlugin")(registry);
    mLibraries.emplace(file, library);
    return true;
}

bool PluginManager::releasePlugin(const std::filesystem::path& path)
{
    const auto file = absolutePluginPath(path);
    std::scoped_lock lock(mLibrariesMutex, mClassDescsMutex);
    const auto found = mLibraries.find(file);
    if (found == mLibraries.end())
        return false;

    std::erase_if(mClassDescs, [&](const auto& entry) { return entry.second->library == found->second; });
    mLibraries.erase(found);
    return true;
}

void PluginManager::loadAllPlugins()
{
    const auto index = runtimeDirectory() / "plugins" / "plugins.json";
    std::ifstream stream{index};
    CHECK(stream, "cannot read render plugin index '{}'", index.string());
    const auto entries = nlohmann::json::parse(stream, nullptr, false);
    CHECK(entries.is_array(), "invalid render plugin index '{}'", index.string());

    for (const auto& entry : entries)
    {
        const auto* name = entry.get_ptr<const std::string*>();
        CHECK(name, "invalid plugin name in '{}'", index.string());
        loadPluginByName(*name);
    }
}

void PluginManager::releaseAllPlugins()
{
    std::scoped_lock lock(mLibrariesMutex, mClassDescsMutex);
    mClassDescs.clear();
    mLibraries.clear();
}

std::shared_ptr<void> PluginManager::retainLibrary(const Library& library, std::shared_ptr<void> object)
{
    // The host destroys the plugin object before releasing the shared library that contains its code.
    struct Owner
    {
        Library library;
        std::shared_ptr<void> object;
    };
    return std::make_shared<Owner>(library, std::move(object));
}
