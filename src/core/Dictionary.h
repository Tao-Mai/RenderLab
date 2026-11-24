#pragma once

#include "core/Logger.h"

#include <any>
#include <string>
#include <unordered_map>
#include <utility>

class Dictionary
{
public:
    class Value
    {
    public:
        Value() = default;
        Value(std::any& value) : mValue(value) {}

        template <typename T>
        void operator=(const T& value)
        {
            mValue = value;
        }

        template <typename T>
        operator T() const
        {
            const auto* value = std::any_cast<T>(&mValue);
            CHECK(value);
            return *value;
        }

    private:
        std::any mValue;
    };

    using Container = std::unordered_map<std::string, Value>;

    Dictionary() = default;
    Dictionary(const Dictionary&) = default;

    Value& operator[](const std::string& key) { return mContainer[key]; }
    const Value& operator[](const std::string& key) const
    {
        const auto found = mContainer.find(key);
        CHECK(found != mContainer.end(), "missing dictionary key '{}'", key);
        return found->second;
    }

    [[nodiscard]] Container::const_iterator begin() const { return mContainer.begin(); }
    [[nodiscard]] Container::const_iterator end() const { return mContainer.end(); }
    [[nodiscard]] Container::iterator begin() { return mContainer.begin(); }
    [[nodiscard]] Container::iterator end() { return mContainer.end(); }
    [[nodiscard]] size_t size() const { return mContainer.size(); }
    [[nodiscard]] bool keyExists(const std::string& key) const { return mContainer.contains(key); }

    template <typename T>
    [[nodiscard]] T getValue(const std::string& key)
    {
        return static_cast<T>(std::as_const(*this)[key]);
    }

    template <typename T>
    [[nodiscard]] T getValue(const std::string& key, const T& defaultValue)
    {
        const auto found = mContainer.find(key);
        return found == mContainer.end() ? defaultValue : static_cast<T>(found->second);
    }

private:
    Container mContainer;
};
