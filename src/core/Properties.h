#pragma once

#include "core/Macros.h"
#include "core/Serializer.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

struct _object;

namespace Falcor
{
class Properties;

namespace detail
{
class DummyArchive;

template <typename T>
inline constexpr bool has_serialize_v = requires(T& value, DummyArchive& archive) { value.serialize(archive); };
}  // namespace detail

template <typename T>
[[nodiscard]] Properties serializeToProperties(const T& value);

template <typename T>
[[nodiscard]] T deserializeFromProperties(const Properties& properties);

class FALCOR_API Properties
{
public:
    using json = nlohmann::ordered_json;

    Properties();
    Properties(const json& value);
    Properties(json&& value);
    // Conversion failures set the Python error indicator.
    explicit Properties(_object* dict);
    Properties(const Properties& other);
    Properties(Properties&& other) noexcept;
    ~Properties();
    Properties& operator=(const Properties& other);
    Properties& operator=(Properties&& other) noexcept;

    [[nodiscard]] json toJson() const;
    // Returns a new Python reference.
    [[nodiscard]] _object* toPython() const;
    [[nodiscard]] std::string dump(int indent = -1) const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool has(std::string_view name) const;

    template <typename T>
    void set(std::string_view name, const T& value)
    {
        if constexpr (detail::has_serialize_v<T>)
            setInternal(name, serializeToProperties(value));
        else
            setInternal(name, value);
    }

    void set(std::string_view name, const char* value) { set(name, std::string(value)); }

    template <typename T>
    [[nodiscard]] bool getTo(std::string_view name, T& value) const
    {
        if constexpr (detail::has_serialize_v<T>)
        {
            Properties properties;
            if (!getInternal(name, properties))
                return false;
            value = deserializeFromProperties<T>(properties);
            return true;
        }
        else
            return getInternal(name, value);
    }

    template <typename T>
    [[nodiscard]] T get(std::string_view name) const
    {
        T value{};
        CHECK(getTo(name, value), "missing property '{}'", name);
        return value;
    }

    template <typename T>
    [[nodiscard]] T get(std::string_view name, const T& defaultValue) const
    {
        T value{};
        if (getTo(name, value))
            return value;
        return defaultValue;
    }

    template <typename T>
    [[nodiscard]] std::optional<T> getOpt(std::string_view name) const
    {
        T value{};
        if (!getTo(name, value))
            return std::nullopt;
        return value;
    }

    bool operator==(const Properties& other) const;
    bool operator!=(const Properties& other) const { return !(*this == other); }

    class Value
    {
    public:
        template <typename T>
        void operator=(const T& value) const { mProperties.set(mName, value); }

        template <typename T>
        operator T() const { return mProperties.get<T>(mName); }

    private:
        friend class Properties;

        Value(Properties& properties, std::string_view name) : mProperties(properties), mName(name) {}

        Properties& mProperties;
        std::string mName;
    };

    class ConstValue
    {
    public:
        template <typename T>
        operator T() const { return mProperties.get<T>(mName); }

    private:
        friend class Properties;

        ConstValue(const Properties& properties, std::string_view name) : mProperties(properties), mName(name) {}

        const Properties& mProperties;
        std::string mName;
    };

    class Iterator
    {
    public:
        ~Iterator();
        bool operator==(const Iterator& other) const;
        bool operator!=(const Iterator& other) const { return !(*this == other); }
        Iterator& operator++();
        std::pair<std::string, Value> operator*();

    private:
        friend class Properties;
        struct Impl;
        explicit Iterator(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };

    class ConstIterator
    {
    public:
        ~ConstIterator();
        bool operator==(const ConstIterator& other) const;
        bool operator!=(const ConstIterator& other) const { return !(*this == other); }
        ConstIterator& operator++();
        std::pair<std::string, ConstValue> operator*();

    private:
        friend class Properties;
        struct Impl;
        explicit ConstIterator(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> mImpl;
    };

    [[nodiscard]] Value operator[](std::string_view name) { return Value(*this, name); }
    [[nodiscard]] const ConstValue operator[](std::string_view name) const { return ConstValue(*this, name); }
    [[nodiscard]] Iterator begin();
    [[nodiscard]] Iterator end();
    [[nodiscard]] ConstIterator begin() const;
    [[nodiscard]] ConstIterator end() const;

private:
    template <typename T>
    void setInternal(std::string_view name, const T& value)
    {
        if constexpr (std::is_same_v<T, Properties>)
            (*mJson)[std::string{name}] = *value.mJson;
        else
            (*mJson)[std::string{name}] = Serialize(value);
    }

    template <typename T>
    bool getInternal(std::string_view name, T& value) const
    {
        const auto& values = *mJson;
        const auto found = values.find(name);
        if (found == values.end())
            return false;

        if constexpr (std::is_same_v<T, Properties>)
            value = Properties(*found);
        else
            Deserialize(nlohmann::json(*found), value);
        return true;
    }

    std::unique_ptr<json> mJson;
};

class PropertiesWriter
{
public:
    template <typename T>
    void operator()(std::string_view name, const T& value) { mProperties.set(name, value); }

    template <typename T>
    [[nodiscard]] static Properties write(const T& value)
    {
        if constexpr (detail::has_serialize_v<T>)
        {
            PropertiesWriter writer;
            const_cast<T&>(value).serialize(writer);
            return std::move(writer.mProperties);
        }
        else
            return Properties::json(Serialize(value));
    }

private:
    PropertiesWriter() = default;
    Properties mProperties;
};

class PropertiesReader
{
public:
    template <typename T>
    void operator()(std::string_view name, T& value) { (void)mProperties.getTo(name, value); }

    template <typename T>
    [[nodiscard]] static T read(const Properties& properties)
    {
        T value{};
        if constexpr (detail::has_serialize_v<T>)
        {
            PropertiesReader reader(properties);
            value.serialize(reader);
        }
        else if (!properties.empty())
        {
            auto data = Serialize(value);
            data.update(nlohmann::json(properties.toJson()), true);
            Deserialize(data, value);
        }
        return value;
    }

private:
    explicit PropertiesReader(const Properties& properties) : mProperties(properties) {}
    const Properties& mProperties;
};

template <typename T>
[[nodiscard]] Properties serializeToProperties(const T& value)
{
    return PropertiesWriter::write(value);
}

template <typename T>
[[nodiscard]] T deserializeFromProperties(const Properties& properties)
{
    return PropertiesReader::read<T>(properties);
}
}  // namespace Falcor
