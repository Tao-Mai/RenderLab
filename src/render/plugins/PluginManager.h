#pragma once

#include "render/graph/RenderPass.h"

#include <map>
#include <memory>
#include <meta>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define RENDERLAB_PLUGIN_EXPORT __declspec(dllexport)
#else
#define RENDERLAB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

struct PassEditorContext;

struct PassEditor
{
    std::string label;
    std::optional<nlohmann::json> (*draw)(const RenderPass&, const PassEditorContext&) = nullptr;
};

class PluginRegistry;

class PluginManager
{
public:
    PluginManager() = default;
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    void loadAllPlugins();
    void clear() noexcept;
    [[nodiscard]] std::shared_ptr<RenderPass> createPass(std::string_view type) const;
    [[nodiscard]] const PassEditor& editor(std::string_view type) const;

private:
    using Library = std::shared_ptr<void>;
    using CreatePass = RenderPass* (*)();

    struct Factory
    {
        Library    library;
        CreatePass create;
        PassEditor editor;
    };

    std::vector<Library> libraries;
    std::map<std::string, Factory, std::less<>> factories;

    void addFactory(std::string_view type, const Library& library, CreatePass create, PassEditor editor);
    friend class PluginRegistry;
};

class PluginRegistry
{
public:
    template <class Pass>
    void registerClass(PassEditor editor = {}, std::string_view type = std::meta::identifier_of(^^Pass))
    {
        manager.addFactory(type, library, []() -> RenderPass* { return new Pass; }, std::move(editor));
    }

private:
    PluginRegistry(PluginManager& manager, const PluginManager::Library& library) :
        manager(manager), library(library)
    {
    }

    PluginManager&                manager;
    const PluginManager::Library& library;
    friend class PluginManager;
};
