#include "render/ShaderManager.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "core/Logger.h"
#include "render/resource/GpuShader.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <tuple>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
const json& requiredField(const json& value, const std::string& name)
{
    CHECK(value.is_object(), "shader reflection expected an object");
    const auto field = value.find(name);
    CHECK(field != value.end(), "shader reflection missing '{}'", name);
    return *field;
}

std::string requiredString(const json& value, const std::string& name)
{
    const auto& string = requiredField(value, name);
    CHECK(string.is_string(), "shader reflection field '{}' is not a string", name);
    return string.get<std::string>();
}

uint32_t requiredIndex(const json& value, const std::string& name)
{
    const auto& number = requiredField(value, name);
    CHECK(number.is_number_unsigned() ||
          (number.is_number_integer() && number.get<int64_t>() >= 0),
          "shader reflection field '{}' is not an index", name);

    const auto index = number.get<uint64_t>();
    CHECK(index <= std::numeric_limits<uint32_t>::max(),
          "shader reflection field '{}' exceeds uint32_t", name);
    return static_cast<uint32_t>(index);
}

std::vector<ShaderMetadata::Binding> readReflection(std::filesystem::path path)
{
    path.replace_extension(".reflection.json");
    std::ifstream input{path, std::ios::binary};
    CHECK(input.is_open(), "failed to open shader reflection '{}'", path.string());
    const auto reflection = json::parse(input, nullptr, false);
    CHECK(!reflection.is_discarded(), "invalid shader reflection JSON '{}'", path.string());

    const auto& parameters = requiredField(reflection, "parameters");
    CHECK(parameters.is_array(), "shader reflection '{}' has no parameters array", path.string());
    std::vector<ShaderMetadata::Binding> bindings;
    for (const auto& parameter : parameters)
    {
        const auto& binding = requiredField(parameter, "binding");
        if (requiredString(binding, "kind") != "descriptorTableSlot")
        {
            continue;
        }
        const auto& type = requiredField(parameter, "type");
        const std::string  kind = requiredString(type, "kind");
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
            if (shape == "structuredBuffer")
            {
                descriptorType = vk::DescriptorType::eStorageBuffer;
            }
            else
            {
                CHECK(shape == "texture2D" || shape == "textureCube",
                      "unsupported reflected resource shape '{}' in '{}'", shape, path.string());
                descriptorType = vk::DescriptorType::eSampledImage;
            }
        }
        else
        {
            LOG_FATAL("unsupported reflected descriptor type '{}' in '{}'",
                      kind,
                      path.string());
        }
        const uint32_t set = binding.contains("space") ? requiredIndex(binding, "space") : 0;
        bindings.push_back({
            set,
            requiredIndex(binding, "index"),
            descriptorType,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        });
    }
    std::sort(bindings.begin(),
              bindings.end(),
              [](const auto& left, const auto& right)
              {
                  return std::tie(left.set, left.binding) < std::tie(right.set, right.binding);
              });
    CHECK(std::adjacent_find(bindings.begin(), bindings.end(),
              [](const auto& left, const auto& right)
              { return left.set == right.set && left.binding == right.binding; }) == bindings.end(),
          "duplicate shader reflection bindings in '{}'",
          path.string());
    return bindings;
}
}

ShaderManager::~ShaderManager() = default;

void ShaderManager::init(const vk::raii::Device& targetDevice)
{
    device = &targetDevice;
    assets = context().assetManager;
}

void ShaderManager::reset() noexcept
{
    handles.clear();
    shaders.clear();
    shaderMetadata.clear();
    assets = nullptr;
    device = nullptr;
}

ShaderHandle ShaderManager::getOrLoad(const ShaderAsset::ID& id)
{
    DCHECK(device && assets, "ShaderManager is not initialized");
    if (const auto found = handles.find(id); found != handles.end())
    {
        return found->second;
    }
    const ShaderAsset& desc = assets->get<ShaderAsset>(id);
    CHECK(!desc.binary.empty(), "shader '{}' has no binary path", id);
    auto               shader = std::make_unique<GpuShader>(*device, desc.binary.string());
    ShaderMetadata     metadata{id, desc.binary, readReflection(desc.binary)};
    const ShaderHandle handle{static_cast<uint32_t>(shaders.size() + 1)};
    shaders.push_back(std::move(shader));
    shaderMetadata.push_back(std::move(metadata));
    handles.emplace(id, handle);
    return handle;
}

vk::ShaderModule ShaderManager::module(ShaderHandle handle) const
{
    DCHECK(handle.index > 0 && handle.index <= shaders.size(),
          "invalid ShaderHandle {}",
          handle.index);
    return shaders[handle.index - 1]->handle();
}

const ShaderMetadata& ShaderManager::metadata(ShaderHandle handle) const
{
    DCHECK(handle.index > 0 && handle.index <= shaderMetadata.size(),
          "invalid ShaderHandle {}",
          handle.index);
    return shaderMetadata[handle.index - 1];
}
