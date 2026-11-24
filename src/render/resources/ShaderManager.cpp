#include "render/resources/ShaderManager.h"

#include "core/Logger.h"
#include "core/RuntimeDirectory.h"
#include "render/device/RhiCheck.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
const std::filesystem::path& shaderDirectory()
{
    static const auto kDirectory = runtimeDirectory() / "shaders";
    return kDirectory;
}

rhi::ComPtr<ISlangBlob> readShader(const std::filesystem::path& path, std::span<const std::string> includes)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    CHECK(input, "missing Slang file '{}'", path.string());
    const auto end = input.tellg();
    CHECK(end > 0, "empty Slang file '{}'", path.string());
    const auto byteCount = static_cast<size_t>(end);

    std::string bytes;
    for (const auto& include : includes)
        bytes.append("#include \"").append(include).append("\"\n");
    if (!includes.empty())
        bytes.append("#line 1\n");
    const auto sourceOffset = bytes.size();
    bytes.resize(sourceOffset + byteCount);
    input.seekg(0);
    CHECK(input.read(bytes.data() + sourceOffset, byteCount), "failed to read '{}'", path.string());
    rhi::ComPtr<ISlangBlob> blob;
    blob.attach(slang_createBlob(bytes.data(), bytes.size()));
    CHECK(blob);
    return blob;
}

std::filesystem::path
findPrecompiled(const std::string& source, const std::vector<std::string>& entries, const ShaderDefines& defines)
{
    const auto    binaries     = shaderDirectory() / "precompiled";
    const auto    manifestPath = binaries / (source + ".variants.json");
    std::ifstream input{manifestPath};
    if (!input)
        return {};

    const auto manifest = json::parse(input, nullptr, false);
    CHECK(manifest.is_array(), "invalid shader manifest '{}'", manifestPath.string());
    const json entryNames = entries;
    const json options    = defines;
    for (const auto& variant : manifest)
    {
        const auto binary         = variant.find("binary");
        const auto variantEntries = variant.find("entries");
        const auto variantDefines = variant.find("defines");
        CHECK(
            binary != variant.end() && binary->is_string() && variantEntries != variant.end() &&
                variantEntries->is_array() && variantDefines != variant.end() && variantDefines->is_object(),
            "invalid shader variant in '{}'",
            manifestPath.string()
        );
        if (*variantEntries == entryNames && *variantDefines == options)
            return binaries / binary->get_ref<const std::string&>();
    }
    return {};
}

rhi::ComPtr<slang::ISession> createSourceSession(slang::IGlobalSession& globalSession, const ShaderDefines& defines)
{
    const auto source         = shaderDirectory() / "source";
    const auto shaderIncludes = (source / "shaders").string();
    const auto sharedHeaders  = (source / "src").string();
    const auto root           = source.string();
    std::array searchPaths{shaderIncludes.c_str(), root.c_str(), sharedHeaders.c_str()};

    std::vector<slang::PreprocessorMacroDesc> macros;
    macros.reserve(defines.size());
    for (const auto& [name, value] : defines)
        macros.push_back({name.c_str(), value.c_str()});

    slang::TargetDesc target;
    target.format                      = SLANG_SPIRV;
    target.profile                     = globalSession.findProfile("spirv_1_4");
    target.forceGLSLScalarBufferLayout = true;

    slang::SessionDesc desc;
    desc.targets                 = &target;
    desc.targetCount             = 1;
    desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    desc.searchPaths             = searchPaths.data();
    desc.searchPathCount         = searchPaths.size();
    desc.preprocessorMacros      = macros.data();
    desc.preprocessorMacroCount  = macros.size();
    rhi::ComPtr<slang::ISession> session;
    rhiCheck(globalSession.createSession(desc, session.writeRef()));
    return session;
}
}  // namespace

void ShaderManager::init(rhi::IDevice* targetDevice) { device = targetDevice; }

void ShaderManager::shutdown() noexcept
{
    variants.clear();
    device = nullptr;
}

rhi::IShaderProgram* ShaderManager::getProgram(
    std::string_view                   source,
    std::vector<std::string>           entries,
    const ShaderDefines&               defines,
    std::span<const std::string_view>  includes
)
{
    ProgramKey key{std::string{source}, std::move(entries), defines, {includes.begin(), includes.end()}};
    if (const auto found = variants.find(key); found != variants.end())
        return found->second.shader;

    const auto binary =
        key.includes.empty() ? findPrecompiled(key.source, key.entries, defines) : std::filesystem::path{};
    const auto                   path = binary.empty() ? shaderDirectory() / "source" / key.source : binary;
    rhi::ComPtr<slang::ISession> deviceSession;
    rhiCheck(device->getSlangSession(deviceSession.writeRef()));
    rhi::ComPtr<slang::ISession> session =
        binary.empty() ? createSourceSession(*deviceSession->getGlobalSession(), defines) : deviceSession;
    const auto              name     = path.stem().string();
    const auto              filename = path.string();
    const auto              blob     = readShader(path, key.includes);
    rhi::ComPtr<ISlangBlob> diagnostics;
    auto*                   module = binary.empty()
                          ? session->loadModuleFromSource(name.c_str(), filename.c_str(), blob, diagnostics.writeRef())
                          : session->loadModuleFromIRBlob(name.c_str(), filename.c_str(), blob, diagnostics.writeRef());
    CHECK(
        module,
        "failed to load Slang module '{}': {}",
        filename,
        diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : ""
    );

    std::vector<rhi::ComPtr<slang::IEntryPoint>> entryPoints;
    std::vector<slang::IComponentType*>          components{module};
    entryPoints.reserve(key.entries.size());
    components.reserve(1 + key.entries.size());
    for (const auto& entryName : key.entries)
    {
        auto& entry = entryPoints.emplace_back();
        rhiCheck(module->findEntryPointByName(entryName.c_str(), entry.writeRef()));
        components.push_back(entry);
    }
    diagnostics.setNull();
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
        filename,
        diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : ""
    );
    rhi::ShaderProgramDesc programDesc;
    programDesc.slangGlobalScope = linked;
    rhi::ComPtr<rhi::IShaderProgram> shader;
    rhiCheck(device->createShaderProgram(programDesc, shader.writeRef()));

    return variants.emplace(std::move(key), Variant{std::move(session), std::move(shader)}).first->second.shader;
}
