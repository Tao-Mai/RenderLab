#pragma once

#include "base/core/scene.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class AssetCache;

class IDemo
{
public:
    virtual ~IDemo() = default;

    [[nodiscard]] virtual const char* name() const = 0;
    [[nodiscard]] virtual Scene&      scene()      = 0;

    // 关联的场景配置路径；空路径表示不可保存。
    [[nodiscard]] virtual std::filesystem::path scene_config_path() const { return {}; }

    virtual void update(float dt) = 0;
    virtual void draw()           = 0;
    virtual void draw_ui()        = 0;

    virtual void on_click(glm::vec2 /*world_xy*/) {}
    virtual void reset() {}

    // 快捷键（GLFW_KEY_* / GLFW_PRESS|RELEASE|REPEAT / GLFW_MOD_*）。
    // 返回 true 表示已消费，Editor 不再做默认处理。
    // ESC 由 Editor 独占退出，不会转发到此处。
    virtual bool on_key(int /*key*/, int /*action*/, int /*mods*/) { return false; }
};

using DemoFactory = std::function<std::unique_ptr<IDemo>(AssetCache&)>;

struct DemoEntry
{
    const char* name;
    DemoFactory factory;
};

// 全局注册表（Meyer singleton），供 REGISTER_DEMO 静态初始化写入。
inline std::vector<DemoEntry>& demo_entries()
{
    static std::vector<DemoEntry> entries;
    return entries;
}

struct DemoRegistrar
{
    DemoRegistrar(const char* name, DemoFactory factory)
    {
        demo_entries().push_back({name, std::move(factory)});
    }
};

#define REGISTER_DEMO(Class, Name)                                                                 \
    static DemoRegistrar g_##Class##_registrar(Name, [](AssetCache& assets) -> std::unique_ptr<IDemo> { \
        return std::make_unique<Class>(assets);                                                    \
    })

class DemoRegistry
{
public:
    DemoRegistry()
    {
        for (const auto& e : demo_entries())
            add(e.name, e.factory);
    }

    void add(const std::string& name, DemoFactory factory)
    {
        entries_.push_back({name, std::move(factory)});
    }

    [[nodiscard]] const std::string& name_at(size_t i) const { return entries_[i].name; }
    [[nodiscard]] size_t             size() const { return entries_.size(); }
    [[nodiscard]] bool               empty() const { return entries_.empty(); }

    std::unique_ptr<IDemo> create(const std::string& name, AssetCache& assets) const
    {
        for (const auto& e : entries_)
            if (e.name == name)
                return e.factory(assets);
        return nullptr;
    }

    [[nodiscard]] int index_of(const std::string& name) const
    {
        for (size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].name == name)
                return static_cast<int>(i);
        return -1;
    }

private:
    struct Entry
    {
        std::string name;
        DemoFactory factory;
    };
    std::vector<Entry> entries_;
};
