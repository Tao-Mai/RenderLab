#include "render/resources/ShaderManager.h"

#include "core/Logger.h"
#include "core/RuntimeDirectory.h"
#include "render/device/RhiCheck.h"

#include <algorithm>
#include <chrono>
#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
const std::filesystem::path& shaderDirectory()
{
    static const auto kDirectory = runtimeDirectory() / "shaders";
    return kDirectory;
}

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

void ShaderManager::init(rhi::IDevice* targetDevice) { device = targetDevice; }

void ShaderManager::shutdown() noexcept
{
    pipelineBuilds.clear();
    variants.clear();
    device = nullptr;
}

rhi::IShaderProgram* ShaderManager::getOrLoad(std::string_view program, const ShaderDefines& defines)
{
    ProgramKey key{std::string{program}, defines};
    if (const auto found = variants.find(key); found != variants.end())
        return found->second.shader;

    const auto    manifestPath = shaderDirectory() / (key.first + ".variants.json");
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
    const auto& selectedDefines = requiredField(*selected, "defines");
    CHECK(selectedDefines.is_object());
    for (const auto& [name, value] : selectedDefines.items())
        CHECK(value.is_string(), "shader define '{}' must be a string", name);

    const auto binary      = manifestPath.parent_path() / requiredString(*selected, "binary");
    const auto entries     = requiredField(*selected, "entries").get<std::vector<std::string>>();
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
    return registerProgram(std::move(key), *module, binary, entries);
}

rhi::IShaderProgram* ShaderManager::getOrCompile(std::string_view program, const ShaderDefines& defines)
{
    ProgramKey key{std::string{program}, defines};
    if (const auto found = variants.find(key); found != variants.end())
        return found->second.shader;

    const auto manifestPath = shaderDirectory() / (key.first + ".source.json");
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
    const auto entries = requiredField(manifest, "entries").get<std::vector<std::string>>();
    return registerProgram(std::move(key), *module, source, entries);
}

rhi::IShaderProgram* ShaderManager::registerProgram(
    ProgramKey key, slang::IModule& module, const std::filesystem::path& path, std::span<const std::string> entries
)
{
    rhi::ComPtr<slang::ISession> session{module.getSession()};
    std::vector<rhi::ComPtr<slang::IEntryPoint>> entryPoints;
    std::vector<slang::IComponentType*>          components{&module};
    for (const auto& entryName : entries)
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

    return variants.emplace(std::move(key), Variant{std::move(session), std::move(shader)}).first->second.shader;
}

std::future<rhi::ComPtr<rhi::IRayTracingPipeline>>
ShaderManager::createRayTracingPipelineAsync(rhi::RayTracingPipelineDesc desc)
{
    std::erase_if(
        pipelineBuilds,
        [](const auto& build) { return build.wait_for(std::chrono::seconds{0}) == std::future_status::ready; }
    );

    // ShaderManager joins the worker at shutdown. The pass can drop the result while the worker runs.
    std::promise<rhi::ComPtr<rhi::IRayTracingPipeline>> promise;
    auto result = promise.get_future();
    pipelineBuilds.push_back(std::async(
        std::launch::async,
        [buildDevice = rhi::ComPtr{device}, shader = rhi::ComPtr{desc.program}, desc, promise = std::move(promise)]() mutable
        {
            desc.program           = shader;
            desc.compilationPolicy = rhi::PipelineCompilationPolicy::Immediate;
            rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
            rhiCheck(buildDevice->createRayTracingPipeline(desc, pipeline.writeRef()));

            auto completion = std::move(promise);
            completion.set_value(std::move(pipeline));
        }
    ));
    return result;
}
