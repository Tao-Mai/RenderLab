#pragma once

#include "core/Logger.h"
#include "core/Object.h"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#define RENDERLAB_PLUGIN_EXPORT __declspec(dllexport)
#else
#define RENDERLAB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

class PluginRegistry;

class PluginManager
{
public:
    [[nodiscard]] static PluginManager& instance();

    template <class BaseT, class... Args>
    std::invoke_result_t<typename BaseT::PluginCreate, Args...> createClass(std::string_view type, Args... args) const
    {
        using Instance = std::invoke_result_t<typename BaseT::PluginCreate, Args...>;
        const auto desc = findClassDesc<BaseT>(type);
        if (!desc)
            return Instance{nullptr};

        auto object = desc->create(std::forward<Args>(args)...);
        if constexpr (std::is_base_of_v<Object, BaseT>)
        {
            if (object)
                object->mPluginLibrary = desc->library;
        }
        return object;
    }

    template <class BaseT>
    bool hasClass(std::string_view type) const
    {
        return findClassDesc<BaseT>(type) != nullptr;
    }

    template <class BaseT>
    std::vector<std::pair<std::string, typename BaseT::PluginInfo>> getInfos() const
    {
        std::lock_guard lock(mClassDescsMutex);
        std::vector<std::pair<std::string, typename BaseT::PluginInfo>> infos;
        for (const auto& [type, entry] : mClassDescs)
            if (const auto* desc = dynamic_cast<const ClassDesc<BaseT>*>(entry.get()))
                infos.emplace_back(type, desc->info);
        return infos;
    }

    bool loadPluginByName(std::string_view name);
    bool loadPlugin(const std::filesystem::path& path);
    bool releasePlugin(const std::filesystem::path& path);
    void loadAllPlugins();
    void releaseAllPlugins();

private:
    using Library = std::shared_ptr<void>;

    struct ClassDescBase
    {
        explicit ClassDescBase(const Library& library) : library(library) {}
        virtual ~ClassDescBase() = default;

        Library library;
    };

    template <class BaseT>
    struct ClassDesc : ClassDescBase
    {
        ClassDesc(
            const Library& library, typename BaseT::PluginInfo info, typename BaseT::PluginCreate create
        ) :
            ClassDescBase(library), info(std::move(info)), create(std::move(create))
        {
        }

        typename BaseT::PluginInfo info;
        typename BaseT::PluginCreate create;
    };

    template <class BaseT>
    void registerClass(
        const Library& library, std::string_view type, typename BaseT::PluginInfo info,
        typename BaseT::PluginCreate create
    )
    {
        auto desc = std::make_shared<ClassDesc<BaseT>>(library, std::move(info), std::move(create));
        auto* pointer = desc.get();
        auto owner = retainLibrary(library, std::move(desc));

        std::lock_guard lock(mClassDescsMutex);
        const bool added = mClassDescs.try_emplace(
            std::string{type}, std::shared_ptr<ClassDescBase>{std::move(owner), pointer}
        ).second;
        CHECK(added, "plugin class '{}' is already registered", type);
    }

    template <class BaseT>
    std::shared_ptr<const ClassDesc<BaseT>> findClassDesc(std::string_view type) const
    {
        std::lock_guard lock(mClassDescsMutex);
        const auto found = mClassDescs.find(type);
        return found == mClassDescs.end() ? nullptr
                                         : std::dynamic_pointer_cast<const ClassDesc<BaseT>>(found->second);
    }

    static std::shared_ptr<void> retainLibrary(const Library& library, std::shared_ptr<void> object);

    std::map<std::filesystem::path, Library> mLibraries;
    std::map<std::string, std::shared_ptr<ClassDescBase>, std::less<>> mClassDescs;
    mutable std::mutex mLibrariesMutex;
    mutable std::mutex mClassDescsMutex;

    friend class PluginRegistry;
};

class PluginRegistry
{
public:
    PluginRegistry(const PluginRegistry&) = delete;
    PluginRegistry& operator=(const PluginRegistry&) = delete;

    template <class BaseT>
    void registerClass(std::string_view type, typename BaseT::PluginInfo info, typename BaseT::PluginCreate create)
    {
        mPluginManager.registerClass<BaseT>(mLibrary, type, std::move(info), std::move(create));
    }

    template <class BaseT, class T>
    void registerClass()
    {
        registerClass<BaseT>(T::kPluginType, T::kPluginInfo, T::create);
    }

private:
    PluginRegistry(PluginManager& pluginManager, const PluginManager::Library& library) :
        mPluginManager(pluginManager), mLibrary(library)
    {
    }

    PluginManager& mPluginManager;
    const PluginManager::Library& mLibrary;

    friend class PluginManager;
};

#define RL_PLUGIN_BASE_CLASS(cls)                                      \
public:                                                                    \
    static_assert(std::is_class_v<cls>);                                    \
    static_assert(std::is_class_v<cls::PluginInfo>);                        \
    static const std::string& getPluginBaseType()                          \
    {                                                                      \
        static const std::string kType{#cls};                              \
        return kType;                                                      \
    }                                                                      \
    virtual const std::string& getPluginType() const = 0;                   \
    virtual const PluginInfo& getPluginInfo() const = 0;

#define RL_PLUGIN_CLASS(cls, type, info)                                  \
public:                                                                       \
    static_assert(std::is_class_v<cls>);                                        \
    static inline const std::string kPluginType{type};                         \
    static inline const PluginInfo kPluginInfo{info};                          \
    const std::string& getPluginType() const final { return kPluginType; }       \
    const PluginInfo& getPluginInfo() const final { return kPluginInfo; }
