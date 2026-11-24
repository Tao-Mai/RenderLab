#include "render/resources/ShaderManager.h"

#include "core/Logger.h"
#include "render/device/RhiCheck.h"
#include "render/resources/CompiledShaderPath.h"
#include "render/resources/ShaderLayout.h"

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

std::vector<char> readShaderBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CHECK(input, "missing Slang file '{}'", path.string());
    const auto byteCount = input.tellg();
    CHECK(byteCount > 0, "empty Slang file '{}'", path.string());
    std::vector<char> bytes(static_cast<size_t>(byteCount));
    input.seekg(0);
    CHECK(input.read(bytes.data(), bytes.size()), "failed to read '{}'", path.string());
    return bytes;
}

}  // namespace

ShaderManager::ShaderManager()  = default;
ShaderManager::~ShaderManager() = default;

void ShaderManager::init(rhi::IDevice* targetDevice) { device = targetDevice; }

void ShaderManager::shutdown() noexcept
{
    handles.clear();
    variants.clear();
    device = nullptr;
}

ShaderHandle ShaderManager::getOrLoad(std::string_view program, const ShaderDefines& defines)
{

    const ProgramKey key{std::string{program}, defines};
    if (const auto found = handles.find(key); found != handles.end())
    {
        return found->second;
    }
    const auto    manifestPath = compiledShaderPath(std::filesystem::path{"shaders"} / (key.first + ".variants.json"));
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
    const auto& options = requiredField(*selected, "defines");
    CHECK(options.is_object());
    ShaderDefines selectedDefines;
    for (const auto& [name, value] : options.items())
    {
        CHECK(value.is_string(), "shader define '{}' must be a string", name);
        selectedDefines.emplace(name, value.get_ref<const std::string&>());
    }
    const auto binary = manifestPath.parent_path() / requiredString(*selected, "binary");

    const auto moduleBytes = readShaderBytes(binary);
    auto session = device->getSlangSession();
    CHECK(session);
    rhi::ComPtr<ISlangBlob> blob;
    blob.attach(slang_createBlob(moduleBytes.data(), moduleBytes.size()));
    CHECK(blob);
    rhi::ComPtr<ISlangBlob> diagnostics;
    auto                    module = session->loadModuleFromIRBlob(
        binary.stem().string().c_str(),
        binary.string().c_str(),
        blob,
        diagnostics.writeRef()
    );
    CHECK(
        module,
        "failed to load '{}': {}",
        binary.string(),
        diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : ""
    );
    ShaderMetadata metadata{
        .binary  = binary,
        .defines = std::move(selectedDefines),
        .entries = selected->at("entries").get<std::vector<std::string>>()
    };
    return registerProgram(key, *module, binary, std::move(metadata));
}

ShaderHandle ShaderManager::getOrCompile(std::string_view program, const ShaderDefines& defines)
{
    const ProgramKey key{std::string{program}, defines};
    if (const auto found = handles.find(key); found != handles.end())
        return found->second;

    const auto manifestPath = compiledShaderPath(std::filesystem::path{"shaders"} / (key.first + ".source.json"));
    std::ifstream input(manifestPath);
    CHECK(input, "missing shader source manifest '{}'", manifestPath.string());
    const auto manifest = json::parse(input, nullptr, false);
    const std::filesystem::path source = requiredString(manifest, "source");
    const auto paths = requiredField(manifest, "searchPaths").get<std::vector<std::string>>();
    std::vector<const char*> searchPaths;
    searchPaths.reserve(paths.size());
    for (const auto& path : paths)
        searchPaths.push_back(path.c_str());

    std::vector<slang::PreprocessorMacroDesc> macros;
    macros.reserve(defines.size());
    for (const auto& [name, value] : defines)
        macros.push_back({name.c_str(), value.c_str()});

    const auto deviceSession = device->getSlangSession();
    CHECK(deviceSession);
    auto* globalSession = deviceSession->getGlobalSession();
    slang::TargetDesc target;
    target.format = SLANG_SPIRV;
    target.profile = globalSession->findProfile("spirv_1_4");
    target.forceGLSLScalarBufferLayout = true;

    slang::SessionDesc desc;
    desc.targets = &target;
    desc.targetCount = 1;
    desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    desc.searchPaths = searchPaths.data();
    desc.searchPathCount = searchPaths.size();
    desc.preprocessorMacros = macros.data();
    desc.preprocessorMacroCount = macros.size();
    rhi::ComPtr<slang::ISession> session;
    rhiCheck(globalSession->createSession(desc, session.writeRef()));

    const auto bytes = readShaderBytes(source);
    rhi::ComPtr<ISlangBlob> blob;
    blob.attach(slang_createBlob(bytes.data(), bytes.size()));
    CHECK(blob);
    rhi::ComPtr<ISlangBlob> diagnostics;
    auto* module = session->loadModuleFromSource(
        source.stem().string().c_str(), source.string().c_str(), blob, diagnostics.writeRef()
    );
    CHECK(
        module,
        "failed to compile '{}': {}",
        source.string(),
        diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : ""
    );
    ShaderMetadata metadata{
        .defines = defines,
        .entries = requiredField(manifest, "entries").get<std::vector<std::string>>()
    };
    return registerProgram(key, *module, source, std::move(metadata));
}

ShaderHandle ShaderManager::registerProgram(
    const ProgramKey& key, slang::IModule& module, const std::filesystem::path& path, ShaderMetadata metadata
)
{
    rhi::ComPtr<slang::ISession> session{module.getSession()};
    std::vector<rhi::ComPtr<slang::IEntryPoint>> entryPoints;
    std::vector<slang::IComponentType*>          components{&module};
    for (const auto& entryName : metadata.entries)
    {
        auto& entry = entryPoints.emplace_back();
        rhiCheck(module.findEntryPointByName(entryName.c_str(), entry.writeRef()));
        components.push_back(entry);
    }
    rhi::ComPtr<ISlangBlob> diagnostics;
    rhi::ComPtr<slang::IComponentType> composite;
    rhiCheck(session->createCompositeComponentType(
        components.data(),
        components.size(),
        composite.writeRef(),
        diagnostics.writeRef()
    ));
    rhi::ComPtr<slang::IComponentType> linked;
    const auto                         linkResult = composite->link(linked.writeRef(), diagnostics.writeRef());
    CHECK(
        SLANG_SUCCEEDED(linkResult),
        "failed to link '{}': {}",
        path.string(),
        diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : ""
    );
    rhi::ShaderProgramDesc programDesc;
    programDesc.slangGlobalScope = linked;
    rhi::ComPtr<rhi::IShaderProgram> shader;
    rhiCheck(device->createShaderProgram(programDesc, shader.writeRef()));

    const ShaderHandle handle{static_cast<uint32_t>(variants.size() + 1)};
    variants.push_back({.session = std::move(session), .shader = std::move(shader), .metadata = std::move(metadata)});
    handles.emplace(key, handle);
    return handle;
}

rhi::IShaderProgram* ShaderManager::program(ShaderHandle handle) const { return variants[handle.index - 1].shader; }

const ShaderMetadata& ShaderManager::metadata(ShaderHandle handle) const { return variants[handle.index - 1].metadata; }

const ShaderLayout& ShaderManager::layout(ShaderHandle handle)
{

    auto& variant = variants[handle.index - 1];
    if (!variant.layout)
    {
        if (variant.metadata.binary.empty())
        {
            auto* reflection = variant.shader->getSlangComponentType()->getLayout();
            CHECK(reflection);
            variant.layout = std::make_unique<ShaderLayout>(*reflection);
        }
        else
        {
            auto reflection = variant.metadata.binary;
            reflection.replace_extension(".reflection.json");
            variant.layout = std::make_unique<ShaderLayout>(reflection);
        }
    }
    return *variant.layout;
}
