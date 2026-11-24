#pragma once
#include <filesystem>
#include <map>
#include <slang-rhi.h>
#include <string>
#include <string_view>
#include "core/Logger.h"
#include "core/Metadata.h"
struct ShaderStorageImage
{
    std::string name;
    rhi::Format format;
};
class ShaderLayout
{
public:
    explicit ShaderLayout(const std::filesystem::path& reflection);
    [[nodiscard]] ShaderStorageImage storageImage(std::string_view name) const;
    template <class T>
    void validateConstants(std::string_view name) const
    {
        const auto found = parameters.find(name);
        CHECK(found != parameters.end(), "shader parameter '{}' is missing", name);
        const auto& parameter = found->second;
        CHECK(
            parameter.size == sizeof(T),
            "shader parameter '{}' has {} bytes; C++ has {}",
            name,
            parameter.size,
            sizeof(T)
        );
        for (const auto& field : metadata::Layout<T>::kFields)
        {
            const auto member = parameter.fields.find(field.name);
            CHECK(member != parameter.fields.end(), "shader field '{}.{}' is missing", name, field.name);
            const auto& reflected = member->second;
            CHECK(
                reflected.offset == field.offset && reflected.size == field.size,
                "shader field '{}.{}' differs from C++",
                name,
                field.name
            );
        }
    }

private:
    struct Field
    {
        size_t offset = 0;
        size_t size   = 0;
    };
    struct Parameter
    {
        size_t                                    size = 0;
        std::string                               imageFormat;
        std::map<std::string, Field, std::less<>> fields;
    };
    std::map<std::string, Parameter, std::less<>> parameters;
};
