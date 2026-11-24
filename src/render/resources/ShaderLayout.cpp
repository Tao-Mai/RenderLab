#include "render/resources/ShaderLayout.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace
{
struct StorageFormat
{
    std::string_view name;
    SlangImageFormat slangFormat;
    rhi::Format      rhiFormat;
};

constexpr StorageFormat kFormats[]{
    {"r32f", SLANG_IMAGE_FORMAT_r32f, rhi::Format::R32Float},
    {"r32ui", SLANG_IMAGE_FORMAT_r32ui, rhi::Format::R32Uint},
    {"rg16f", SLANG_IMAGE_FORMAT_rg16f, rhi::Format::RG16Float},
    {"rgba16f", SLANG_IMAGE_FORMAT_rgba16f, rhi::Format::RGBA16Float},
    {"rgba32f", SLANG_IMAGE_FORMAT_rgba32f, rhi::Format::RGBA32Float},
    {"rgba8", SLANG_IMAGE_FORMAT_rgba8, rhi::Format::RGBA8Unorm}
};
}  // namespace

ShaderLayout::ShaderLayout(const std::filesystem::path& path)
{
    std::ifstream input(path);
    CHECK(input, "missing shader reflection '{}'", path.string());
    const auto document = nlohmann::json::parse(input, nullptr, false);
    CHECK(
        document.is_object() && document.contains("parameters") && document.contains("entryPoints"),
        "invalid shader reflection '{}'",
        path.string()
    );
    for (const auto& variable : document.at("parameters"))
    {
        auto&       parameter = parameters[variable.at("name").get<std::string>()];
        const auto& type      = variable.at("type");
        if (type.at("kind") == "constantBuffer")
        {
            parameter.size      = type.at("elementVarLayout").at("binding").at("size");
            const auto& element = type.at("elementType");
            if (element.at("kind") == "struct")
                for (const auto& field : element.at("fields"))
                    parameter.fields[field.at("name").get<std::string>()] = {
                        field.at("binding").at("offset"),
                        field.at("binding").at("size")
                    };
        }
    }
    for (const auto& entry : document.at("entryPoints"))
        for (const auto& resource : entry.at("bindings"))
            if (resource.contains("format"))
                parameters[resource.at("name").get<std::string>()].imageFormat = resource.at("format");
}

ShaderLayout::ShaderLayout(slang::ProgramLayout& reflection)
{
    for (unsigned index = 0; index < reflection.getParameterCount(); ++index)
    {
        auto*       variable = reflection.getParameterByIndex(index);
        const auto* name = variable->getName();
        if (!name)
            continue;
        auto& parameter = parameters[name];
        auto* type      = variable->getTypeLayout();
        if (type->getKind() == slang::TypeReflection::Kind::ConstantBuffer)
        {
            auto* element  = type->getElementTypeLayout();
            parameter.size = element->getSize();
            for (unsigned fieldIndex = 0; fieldIndex < element->getFieldCount(); ++fieldIndex)
            {
                auto* field = element->getFieldByIndex(fieldIndex);
                parameter.fields[field->getName()] = {field->getOffset(), field->getTypeLayout()->getSize()};
            }
        }
        const auto imageFormat = variable->getImageFormat();
        for (const auto& format : kFormats)
            if (format.slangFormat == imageFormat)
            {
                parameter.imageFormat = format.name;
                break;
            }
    }
}

ShaderStorageImage ShaderLayout::storageImage(std::string_view name) const
{
    const auto found = parameters.find(name);
    CHECK(found != parameters.end(), "shader image '{}' is missing", name);
    const auto& format = found->second.imageFormat;
    for (const auto& entry : kFormats)
        if (format == entry.name)
            return {std::string(name), entry.rhiFormat};
    LOG_FATAL("unsupported storage image format '{}' for '{}'", format, name);
}
