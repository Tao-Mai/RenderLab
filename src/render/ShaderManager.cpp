#include "render/ShaderManager.h"

#include "asset/AssetDesc.h"
#include "asset/AssetDescManager.h"
#include "core/Logger.h"
#include "render/resource/Shader.h"

#include <algorithm>
#include <tuple>

#include <rfl/json.hpp>

namespace
{
rfl::Generic requiredField(const rfl::Generic& value, const std::string& name)
{
    const auto object = value.to_object();
    CHECK(object, "shader reflection expected an object: {}", object.error().what());
    const auto field = object->get(name);
    CHECK(field, "shader reflection missing '{}': {}", name, field.error().what());
    return *field;
}

std::string requiredString(const rfl::Generic& value, const std::string& name)
{
    const auto string = requiredField(value, name).to_string();
    CHECK(string, "shader reflection field '{}' is not a string", name);
    return *string;
}

uint32_t requiredIndex(const rfl::Generic& value, const std::string& name)
{
    const auto number = requiredField(value, name).to_int();
    CHECK(number && *number >= 0, "shader reflection field '{}' is not an index", name);
    return static_cast<uint32_t>(*number);
}

std::vector<ShaderMetadata::Binding> readReflection(std::filesystem::path path)
{
    path.replace_extension(".reflection.json");
    const auto reflection = rfl::json::load<rfl::Generic>(path.string());
    CHECK(reflection, "failed to load shader reflection '{}': {}",
        path.string(), reflection.error().what());
    const auto parameters = requiredField(*reflection, "parameters").to_array();
    CHECK(parameters, "shader reflection '{}' has no parameters array", path.string());
    std::vector<ShaderMetadata::Binding> bindings;
    for (const rfl::Generic& parameter : *parameters)
    {
        const rfl::Generic binding = requiredField(parameter, "binding");
        if (requiredString(binding, "kind") != "descriptorTableSlot")
        {
            continue;
        }
        const rfl::Generic type = requiredField(parameter, "type");
        const std::string kind = requiredString(type, "kind");
        vk::DescriptorType descriptorType;
        if (kind == "constantBuffer")
        {
            descriptorType = vk::DescriptorType::eUniformBuffer;
        }
        else if (kind == "samplerState")
        {
            descriptorType = vk::DescriptorType::eSampler;
        }
        else if (kind == "resource")
        {
            const std::string shape = requiredString(type, "baseShape");
            CHECK(shape == "texture2D" || shape == "textureCube",
                "unsupported reflected resource shape '{}' in '{}'",
                shape, path.string());
            descriptorType = vk::DescriptorType::eSampledImage;
        }
        else
        {
            LOG_FATAL("unsupported reflected descriptor type '{}' in '{}'",
                kind, path.string());
        }
        const auto bindingObject = binding.to_object();
        CHECK(bindingObject, "invalid shader reflection binding");
        const auto space = bindingObject->get("space");
        uint32_t set = 0;
        if (space)
        {
            const auto parsed = space->to_int();
            CHECK(parsed && *parsed >= 0, "invalid descriptor set in '{}'",
                path.string());
            set = static_cast<uint32_t>(*parsed);
        }
        bindings.push_back({
            set,
            requiredIndex(binding, "index"),
            descriptorType,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        });
    }
    std::sort(bindings.begin(), bindings.end(),
        [](const auto& left, const auto& right)
        { return std::tie(left.set, left.binding) < std::tie(right.set, right.binding); });
    CHECK(std::adjacent_find(bindings.begin(), bindings.end(),
        [](const auto& left, const auto& right)
        { return left.set == right.set && left.binding == right.binding; }) == bindings.end(),
        "duplicate shader reflection bindings in '{}'", path.string());
    return bindings;
}
}

ShaderManager::~ShaderManager() = default;

void ShaderManager::init(const vk::raii::Device& targetDevice, AssetDescManager& targetAssets)
{
    device = &targetDevice;
    assets = &targetAssets;
}

void ShaderManager::reset() noexcept
{
    handles.clear();
    shaders.clear();
    shaderMetadata.clear();
    assets = nullptr;
    device = nullptr;
}

ShaderHandle ShaderManager::getOrLoad(const AssetId& id)
{
    CHECK(device != nullptr && assets != nullptr, "ShaderManager is not initialized");
    if (const auto found = handles.find(id); found != handles.end())
    {
        return found->second;
    }
    const ShaderDesc& desc = assets->desc<ShaderDesc>(id);
    CHECK(!desc.binary.empty(), "shader '{}' has no binary path", id);
    auto shader = std::make_unique<Shader>(*device, desc.binary.string());
    ShaderMetadata metadata{id, desc.binary, readReflection(desc.binary)};
    const ShaderHandle handle{static_cast<uint32_t>(shaders.size() + 1)};
    shaders.push_back(std::move(shader));
    shaderMetadata.push_back(std::move(metadata));
    handles.emplace(id, handle);
    return handle;
}

vk::ShaderModule ShaderManager::module(ShaderHandle handle) const
{
    CHECK(handle.index > 0 && handle.index <= shaders.size(),
        "invalid ShaderHandle {}", handle.index);
    return shaders[handle.index - 1]->handle();
}

const ShaderMetadata& ShaderManager::metadata(ShaderHandle handle) const
{
    CHECK(handle.index > 0 && handle.index <= shaderMetadata.size(),
        "invalid ShaderHandle {}", handle.index);
    return shaderMetadata[handle.index - 1];
}
