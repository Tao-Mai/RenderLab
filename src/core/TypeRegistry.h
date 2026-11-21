#pragma once

#include <memory>
#include <string_view>
#include <typeindex>
#include <unordered_map>

#include <nlohmann/json_fwd.hpp>

template <class Base>
struct TypeRegistry
{
    struct Entry
    {
        const char* id;
        std::string_view parent;
        std::unique_ptr<Base> (*create)();
        nlohmann::json (*serialize)(const Base&);
        std::unique_ptr<Base> (*deserialize)(const nlohmann::json&);
    };

    // Instantiated in the module that owns this type registry.
    static TypeRegistry& get();

    std::unordered_map<std::string_view, Entry> entries;
    std::unordered_map<std::type_index, const Entry*> nativeTypes;
};
