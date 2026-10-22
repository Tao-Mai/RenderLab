#include "render/ShaderManager.h"

#include "asset/AssetManager.h"
#include "asset/ShaderAsset.h"
#include "core/Logger.h"
#include "render/resource/GpuShader.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
const json& requiredField(const json& value, const std::string& name)
{
    CHECK(value.is_object(), "shader manifest expected an object");
    const auto field = value.find(name);
    CHECK(field != value.end(), "shader manifest missing '{}'", name);
    return *field;
}

std::string requiredString(const json& value, const std::string& name)
{
    const auto& string = requiredField(value, name);
    CHECK(string.is_string(), "shader manifest field '{}' is not a string", name);
    return string.get<std::string>();
}

}  // namespace

ShaderManager::~ShaderManager() { DCHECK(!device); }

void ShaderManager::init(const vk::raii::Device& targetDevice, AssetManager& targetAssets)
{
    device = &targetDevice;
    assets = &targetAssets;
}

void ShaderManager::shutdown() noexcept
{
    handles.clear();
    shaders.clear();
    shaderMetadata.clear();
    assets = nullptr;
    device = nullptr;
}

ShaderHandle ShaderManager::getOrLoad(const ShaderAsset::ID& id, const ShaderDefines& defines)
{
    DCHECK(device && assets);
    const auto key = std::pair{id.value, defines};
    if (const auto found = handles.find(key); found != handles.end())
    {
        return found->second;
    }
    const ShaderAsset& desc = assets->get<ShaderAsset>(id);
    CHECK(!desc.binary.empty(), "shader '{}' has no binary path", id);
    auto manifestPath = desc.binary;
    manifestPath.replace_extension(".variants.json");
    std::ifstream input{manifestPath};
    CHECK(input.is_open(), "missing shader manifest '{}'", manifestPath.string());
    const auto manifest = json::parse(input, nullptr, false);
    CHECK(manifest.is_array() && !manifest.empty(), "invalid shader manifest '{}'", manifestPath.string());
    const json* selected = defines.empty() ? &manifest.front() : nullptr;
    if (!selected)
        for (const auto& variant : manifest)
        {
            const auto& options = requiredField(variant, "defines");
            if (std::ranges::all_of(
                    defines,
                    [&](const auto& option)
                    { return options.contains(option.first) && options[option.first] == option.second; }
                ))
            {
                CHECK(!selected, "ambiguous shader options for '{}'", id);
                selected = &variant;
            }
        }
    CHECK(selected, "shader '{}' has no matching compiled variant", id);
    const auto     binary = manifestPath.parent_path() / requiredString(*selected, "binary");
    auto           shader = std::make_unique<GpuShader>(*device, binary.string());
    ShaderMetadata metadata{
        .id      = id,
        .binary  = binary,
        .defines = selected->at("defines").get<ShaderDefines>(),
        .entries = selected->at("entries").get<std::vector<std::string>>()
    };
    const ShaderHandle handle{static_cast<uint32_t>(shaders.size() + 1)};
    shaders.push_back(std::move(shader));
    shaderMetadata.push_back(std::move(metadata));
    handles.emplace(key, handle);
    return handle;
}

vk::ShaderModule ShaderManager::module(ShaderHandle handle) const
{
    DCHECK(handle.index > 0 && handle.index <= shaders.size(), "invalid ShaderHandle {}", handle.index);
    return shaders[handle.index - 1]->handle();
}

const ShaderMetadata& ShaderManager::metadata(ShaderHandle handle) const
{
    DCHECK(handle.index > 0 && handle.index <= shaderMetadata.size(), "invalid ShaderHandle {}", handle.index);
    return shaderMetadata[handle.index - 1];
}
