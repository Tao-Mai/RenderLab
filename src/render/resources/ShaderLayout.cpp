#include "render/resources/ShaderLayout.h"

#include "core/Logger.h"
#include "render/device/Device.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

namespace
{
using Json = nlohmann::json;

const Json& field(const Json& object, const char* name)
{
    const auto value = object.find(name);
    CHECK(value != object.end(), "Slang reflection is missing '{}'", name);
    return *value;
}

const Json& array(const Json& object, const char* name)
{
    const auto& value = field(object, name);
    CHECK(value.is_array(), "Slang reflection '{}' must be an array", name);
    return value;
}

std::string_view string(const Json& object, const char* name)
{
    const auto& value = field(object, name);
    CHECK(value.is_string(), "Slang reflection '{}' must be a string", name);
    return value.get_ref<const std::string&>();
}

uint32_t unsignedNumber(const Json& value, const char* name)
{
    CHECK(value.is_number_unsigned(), "Slang reflection '{}' must be an unsigned integer", name);
    const auto result = value.get<uint64_t>();
    CHECK(result <= std::numeric_limits<uint32_t>::max(), "Slang reflection '{}' exceeds uint32", name);
    return static_cast<uint32_t>(result);
}

uint32_t number(const Json& object, const char* name)
{
    return unsignedNumber(field(object, name), name);
}

vk::DescriptorType descriptorType(const Json& type)
{
    const auto kind = string(type, "kind");
    if (kind == "constantBuffer")
        return vk::DescriptorType::eUniformBuffer;
    if (kind == "samplerState")
        return vk::DescriptorType::eSampler;

    CHECK(kind == "resource", "unsupported Slang descriptor kind '{}'", kind);
    const auto shape = string(type, "baseShape");
    if (shape == "accelerationStructure")
        return vk::DescriptorType::eAccelerationStructureKHR;
    if (shape == "structuredBuffer" || shape == "byteAddressBuffer")
        return vk::DescriptorType::eStorageBuffer;

    CHECK(shape.starts_with("texture"), "unsupported Slang resource shape '{}'", shape);
    if (!type.contains("access"))
        return vk::DescriptorType::eSampledImage;

    const auto access = string(type, "access");
    CHECK(access == "readWrite", "unsupported Slang texture access '{}'", access);
    return vk::DescriptorType::eStorageImage;
}

constexpr std::pair<std::string_view, vk::Format> kImageFormats[]{
    {"r32f", vk::Format::eR32Sfloat},
    {"r32ui", vk::Format::eR32Uint},
    {"rg16f", vk::Format::eR16G16Sfloat},
    {"rgba16f", vk::Format::eR16G16B16A16Sfloat},
    {"rgba32f", vk::Format::eR32G32B32A32Sfloat},
    {"rgba8", vk::Format::eR8G8B8A8Unorm},
};
}

ShaderLayout::ShaderLayout(const std::filesystem::path& reflection) : source(reflection)
{
    std::ifstream input{source};
    CHECK(input.is_open(), "missing shader reflection '{}'", source.string());
    const auto document = Json::parse(input, nullptr, false);
    CHECK(document.is_object(), "invalid shader reflection '{}'", source.string());

    for (const auto& variable : array(document, "parameters"))
    {
        const auto name = string(variable, "name");
        const auto& reflectedBinding = field(variable, "binding");
        const auto bindingKind = string(reflectedBinding, "kind");
        Parameter parameter;
        parameter.binding = number(reflectedBinding, "index");
        const auto* type = &field(variable, "type");

        if (bindingKind == "pushConstantBuffer")
        {
            CHECK(parameter.binding == 0 && string(*type, "kind") == "constantBuffer",
                  "unsupported push constant block '{}' in '{}'", name, source.string());
            parameter.pushConstant = true;
        }
        else
        {
            CHECK(bindingKind == "descriptorTableSlot", "unsupported binding kind '{}' for '{}' in '{}'",
                  bindingKind, name, source.string());
            // Slang omits the space member for descriptor set zero.
            parameter.set = reflectedBinding.contains("space") ? number(reflectedBinding, "space") : 0;
            while (string(*type, "kind") == "array")
            {
                const auto count = number(*type, "elementCount");
                CHECK(count != 0 && count <= std::numeric_limits<uint32_t>::max() / parameter.count,
                      "unsupported descriptor array size for '{}' in '{}'", name, source.string());
                parameter.count *= count;
                type = &field(*type, "elementType");
            }
            parameter.descriptorType = descriptorType(*type);
            if (parameter.descriptorType == vk::DescriptorType::eStorageImage)
                parameter.image2D = string(*type, "baseShape") == "texture2D";
        }

        if (string(*type, "kind") == "constantBuffer")
        {
            const auto& element = field(*type, "elementVarLayout");
            parameter.uniformSize = number(field(element, "binding"), "size");
            const auto& elementType = field(*type, "elementType");
            if (string(elementType, "kind") == "struct")
                for (const auto& member : array(elementType, "fields"))
                {
                    const auto& layout = field(member, "binding");
                    const auto memberName = string(member, "name");
                    const bool inserted = parameter.fields.try_emplace(std::string{memberName}, Field{
                        .offset = number(layout, "offset"),
                        .size = number(layout, "size"),
                    }).second;
                    CHECK(inserted, "duplicate field '{}' in shader parameter '{}'", memberName, name);
                }
        }
        const bool inserted = parameters.try_emplace(std::string{name}, std::move(parameter)).second;
        CHECK(inserted, "duplicate shader parameter '{}' in '{}'", name, source.string());
    }

    // The storage image format is recorded with entry-point bindings.
    for (const auto& entry : array(document, "entryPoints"))
    {
        if (const auto stage = entry.find("stage"); stage != entry.end() && *stage == "compute")
        {
            const auto name = string(entry, "name");
            const auto& group = array(entry, "threadGroupSize");
            CHECK(group.size() == 3, "invalid thread group size for '{}' in '{}'", name, source.string());
            const vk::Extent3D size{
                unsignedNumber(group[0], "threadGroupSize"),
                unsignedNumber(group[1], "threadGroupSize"),
                unsignedNumber(group[2], "threadGroupSize"),
            };
            CHECK(size.width && size.height && size.depth, "empty thread group for '{}' in '{}'", name, source.string());
            const bool inserted = computeThreadGroups.try_emplace(std::string{name}, size).second;
            CHECK(inserted, "duplicate compute entry '{}' in '{}'", name, source.string());
        }

        for (const auto& binding : array(entry, "bindings"))
        {
            if (!binding.contains("format"))
                continue;

            const auto name = string(binding, "name");
            const auto format = string(binding, "format");
            const auto parameter = parameters.find(name);
            CHECK(parameter != parameters.end(), "unknown shader image '{}' in '{}'", name, source.string());
            auto& recorded = parameter->second.imageFormat;
            CHECK(recorded.empty() || recorded == format, "inconsistent image format for '{}' in '{}'",
                  name, source.string());
            recorded = format;
        }
    }
}

vk::Extent3D ShaderLayout::threadGroupSize(std::string_view entry) const
{
    const auto found = computeThreadGroups.find(entry);
    CHECK(found != computeThreadGroups.end(), "shader reflection '{}' has no compute entry '{}'", source.string(), entry);
    return found->second;
}

const ShaderLayout::Parameter& ShaderLayout::requireParameter(std::string_view name) const
{
    const auto parameter = parameters.find(name);
    CHECK(parameter != parameters.end(), "shader reflection '{}' is missing parameter '{}'", source.string(), name);
    return parameter->second;
}

const ShaderLayout::Parameter& ShaderLayout::requireBinding(
    std::string_view name, uint32_t set, vk::DescriptorType type
) const
{
    const auto& parameter = requireParameter(name);
    CHECK(parameter.set == set && parameter.descriptorType == type && parameter.count == 1,
          "shader parameter '{}' in '{}' must be a single {} in set {}", name, source.string(), vk::to_string(type), set);
    return parameter;
}

const ShaderLayout::Parameter& ShaderLayout::requirePushConstants(std::string_view name) const
{
    const auto& parameter = requireParameter(name);
    CHECK(parameter.pushConstant, "shader parameter '{}' in '{}' must be a push constant block", name, source.string());
    return parameter;
}

ShaderStorageImage ShaderLayout::storageImage(std::string_view name, uint32_t set) const
{
    const auto& parameter = requireBinding(name, set, vk::DescriptorType::eStorageImage);
    CHECK(parameter.image2D, "shader image '{}' in '{}' must be a 2D storage image", name, source.string());
    const auto format = std::ranges::find_if(kImageFormats, [&](const auto& entry)
    {
        return entry.first == parameter.imageFormat;
    });
    CHECK(format != std::end(kImageFormats), "shader image '{}' in '{}' has unsupported format '{}'",
          name, source.string(), parameter.imageFormat);
    return {{parameter.binding}, format->second};
}

void ShaderLayout::requireUniformSize(
    std::string_view name, const Parameter& parameter, size_t size, size_t fields
) const
{
    CHECK(parameter.uniformSize == size && parameter.fields.size() == fields,
          "uniform block '{}' in '{}' has {} bytes and {} fields; C++ has {} bytes and {} fields",
          name, source.string(), parameter.uniformSize, parameter.fields.size(), size, fields);
}

void ShaderLayout::requireUniformField(
    std::string_view name, const Parameter& parameter, std::string_view member, size_t offset, size_t size
) const
{
    const auto field = parameter.fields.find(member);
    CHECK(field != parameter.fields.end(), "uniform block '{}' in '{}' is missing '{}'", name, source.string(), member);
    CHECK(field->second.offset == offset && field->second.size == size,
          "field '{}.{}' in '{}' has offset {} and size {}; C++ has offset {} and size {}",
          name, member, source.string(), field->second.offset, field->second.size, offset, size);
}

void ShaderLayout::validateDescriptorSets(std::span<const DescriptorSetLayout* const> sets) const
{
    for (const auto& [name, parameter] : parameters)
    {
        if (!parameter.descriptorType)
            continue;

        CHECK(parameter.set < sets.size(), "shader parameter '{}' in '{}' has no descriptor set {}",
              name, source.string(), parameter.set);
        const auto& bindings = sets[parameter.set]->bindings;
        const auto binding = std::ranges::find(bindings, parameter.binding, &vk::DescriptorSetLayoutBinding::binding);
        CHECK(binding != bindings.end() && binding->descriptorType == *parameter.descriptorType &&
                  binding->descriptorCount >= parameter.count,
              "shader parameter '{}' in '{}' is incompatible with descriptor set {}, binding {}",
              name, source.string(), parameter.set, parameter.binding);
    }
}
