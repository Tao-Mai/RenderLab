#include "render/resources/ShaderLayout.h"
#include <fstream>
#include <nlohmann/json.hpp>
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

ShaderStorageImage ShaderLayout::storageImage(std::string_view name) const
{
    const auto found = parameters.find(name);
    CHECK(found != parameters.end(), "shader image '{}' is missing", name);
    const auto&                                        format = found->second.imageFormat;
    constexpr std::pair<std::string_view, rhi::Format> kFormats[]{
        {"r32f", rhi::Format::R32Float},
        {"r32ui", rhi::Format::R32Uint},
        {"rg16f", rhi::Format::RG16Float},
        {"rgba16f", rhi::Format::RGBA16Float},
        {"rgba32f", rhi::Format::RGBA32Float},
        {"rgba8", rhi::Format::RGBA8Unorm}
    };
    for (const auto& [text, value] : kFormats)
        if (format == text)
            return {std::string(name), value};
    LOG_FATAL("unsupported storage image format '{}' for '{}'", format, name);
}
