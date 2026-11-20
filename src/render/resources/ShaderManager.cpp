#include "render/resources/ShaderManager.h"

#include "core/Logger.h"
#include "render/resources/CompiledShaderPath.h"
#include "render/resources/GpuShader.h"

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

void ShaderManager::init(const vk::raii::Device& targetDevice)
{
    device = &targetDevice;
}

void ShaderManager::shutdown() noexcept
{
    handles.clear();
    shaders.clear();
    shaderMetadata.clear();
    device = nullptr;
}

ShaderHandle ShaderManager::getOrLoad(std::string_view program, const ShaderDefines& defines)
{
    DCHECK(device);
    const auto key = std::pair{std::string{program}, defines};
    if (const auto found = handles.find(key); found != handles.end())
    {
        return found->second;
    }
    const auto manifestPath = compiledShaderPath(std::filesystem::path{"shaders"} / (key.first + ".variants.json"));
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
                CHECK(!selected, "ambiguous shader options for '{}'", program);
                selected = &variant;
            }
        }
    CHECK(selected, "shader '{}' has no matching compiled variant", program);
    const auto     binary = manifestPath.parent_path() / requiredString(*selected, "binary");
    auto           shader = std::make_unique<GpuShader>(*device, binary.string());
    ShaderMetadata metadata{
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
