#include "core/Metadata.h"

#include "core/Logger.h"

#include <mutex>
#include <shared_mutex>
#include <typeindex>
#include <unordered_map>

namespace metadata
{
namespace
{
template <class T>
using Index = std::unordered_map<std::type_index, const T*>;

struct Registry
{
    std::shared_mutex mutex;
    Index<Record> records;
    Index<EnumInfo> enums;
    Index<std::span<const LayoutField>> layouts;
};

Registry& registry()
{
    static Registry instance;
    return instance;
}

template <class T>
void add(Index<T>& index, std::span<const Entry<T>> entries)
{
    for (const auto& entry : entries)
    {
        const bool inserted = index.emplace(entry.type, &entry.data).second;
        CHECK(inserted, "duplicate metadata for '{}'", entry.type.name());
    }
}

template <class T>
void remove(Index<T>& index, std::span<const Entry<T>> entries)
{
    for (const auto& entry : entries)
        index.erase(entry.type);
}

template <class T>
const T& get(const Index<T>& index, const std::type_info& type)
{
    const std::shared_lock lock(registry().mutex);
    const auto found = index.find(type);
    CHECK(found != index.end(), "missing metadata for '{}'", type.name());
    return *found->second;
}
}

ModuleRegistration::ModuleRegistration(Module module) : module(module)
{
    auto& index = registry();
    const std::unique_lock lock(index.mutex);
    add(index.records, module.records);
    add(index.enums, module.enums);
    add(index.layouts, module.layouts);
}

ModuleRegistration::~ModuleRegistration()
{
    auto& index = registry();
    const std::unique_lock lock(index.mutex);
    remove(index.records, module.records);
    remove(index.enums, module.enums);
    remove(index.layouts, module.layouts);
}

const Record& record(const std::type_info& type) { return get(registry().records, type); }
const EnumInfo& enumInfo(const std::type_info& type) { return get(registry().enums, type); }
std::span<const LayoutField> layoutFields(const std::type_info& type) { return get(registry().layouts, type); }
}
