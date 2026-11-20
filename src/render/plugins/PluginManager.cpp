#include "render/plugins/PluginManager.h"

#include "core/Logger.h"
#include "core/RuntimeDirectory.h"

#include <filesystem>
#include <fstream>

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
std::shared_ptr<void> loadLibrary(const std::filesystem::path& path)
{
#ifdef _WIN32
    const auto module = LoadLibraryExW(
        path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
    );
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
}

void PluginManager::loadAllPlugins()
{
    DCHECK(libraries.empty() && factories.empty());
    const auto directory = runtimeDirectory() / "plugins";
    const auto index     = directory / "plugins.json";
    std::ifstream stream{index};
    CHECK(stream, "cannot read render plugin index '{}'", index.string());
    const auto entries = nlohmann::json::parse(stream, nullptr, false);
    CHECK(entries.is_array(), "invalid render plugin index '{}'", index.string());

    for (const auto& entry : entries)
    {
        const auto* filename = entry.get_ptr<const std::string*>();
        CHECK(filename && !filename->empty(), "invalid plugin name in '{}'", index.string());
        const std::filesystem::path file{*filename};
        CHECK(file == file.filename(), "plugin index entry must be a filename: '{}'", *filename);

        const auto path = directory / file;
        const auto& library = libraries.emplace_back(loadLibrary(path));
        const auto abi = pluginSymbol<const char* (*)()>(library, path, "renderLabPluginAbi");
        const auto* tag = abi();
        CHECK(tag && std::string_view{tag} == RENDERLAB_PLUGIN_ABI, "render plugin '{}' uses a different ABI", path.string());

        PluginRegistry registry{*this, library};
        pluginSymbol<void (*)(PluginRegistry&)>(library, path, "registerPlugin")(registry);
    }
}

void PluginManager::clear() noexcept
{
    factories.clear();
    while (!libraries.empty())
        libraries.pop_back();
}

std::shared_ptr<RenderPass> PluginManager::createPass(std::string_view type) const
{
    const auto found = factories.find(type);
    CHECK(found != factories.end(), "unknown render pass type '{}'", type);
    const auto& factory = found->second;
    return std::shared_ptr<RenderPass>{
        factory.create(),
        [library = factory.library](RenderPass* pass) { delete pass; },
    };
}

const PassEditor& PluginManager::editor(std::string_view type) const
{
    const auto found = factories.find(type);
    DCHECK(found != factories.end());
    return found->second.editor;
}

void PluginManager::addFactory(std::string_view type, const Library& library, CreatePass create, PassEditor editor)
{
    const bool added = factories.try_emplace(std::string{type}, library, create, std::move(editor)).second;
    CHECK(added, "render pass type '{}' is already registered", type);
}
