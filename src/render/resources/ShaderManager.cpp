#include "render/resources/ShaderManager.h"

#include "core/Logger.h"
#include "core/RuntimeDirectory.h"
#include "render/device/RhiCheck.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
namespace fs = std::filesystem;

const fs::path& shaderDirectory()
{
    static const auto kDirectory = runtimeDirectory() / "shaders";
    return kDirectory;
}

const fs::path& cacheDirectory()
{
    static const auto kDirectory = shaderDirectory() / "cache";
    return kDirectory;
}

std::optional<std::string> readFile(const fs::path& file)
{
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input)
        return std::nullopt;
    const auto size = input.tellg();
    if (size < 0)
        return std::nullopt;

    std::string bytes(static_cast<size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        return std::nullopt;
    return bytes;
}

bool writeFile(const fs::path& file, std::string_view bytes)
{
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    if (error)
        return false;

    static std::atomic<uint64_t> nextTemporary{};
    auto temporary = file;
    temporary += std::format(".{}.{}.tmp", std::chrono::steady_clock::now().time_since_epoch().count(),
        nextTemporary.fetch_add(1, std::memory_order_relaxed));
    std::ofstream output(temporary, std::ios::binary | std::ios::noreplace);
    if (!output)
        return false;

    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (output)
    {
        fs::rename(temporary, file, error);
        if (!error)
            return true;
    }
    fs::remove(temporary, error);
    return false;
}

void removeCacheFile(const fs::path& file)
{
    std::error_code error;
    fs::remove(file, error);
    CHECK(!error, "failed to remove shader cache '{}': {}", file.string(), error.message());
}

uint64_t hashIdentity(std::string_view text)
{
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime  = 1099511628211ull;
    uint64_t hash = kFnvOffset;
    for (const unsigned char byte : text)
        hash = (hash ^ byte) * kFnvPrime;
    return hash;
}

std::string_view blobBytes(ISlangBlob& blob)
{
    return {static_cast<const char*>(blob.getBufferPointer()), blob.getBufferSize()};
}

std::string bytecodeKey(ISlangBlob& key)
{
    static const auto kCompiler = std::format("{:016x}", hashIdentity(spGetBuildTagString()));
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string name = kCompiler;
    for (const unsigned char byte : blobBytes(key))
    {
        name += kHex[byte >> 4];
        name += kHex[byte & 0xf];
    }
    return name;
}

fs::path bytecodeFile(std::string_view key)
{
    return cacheDirectory() / "bytecode" / (std::string{key} + ".bin");
}

class ShaderBytecodeCache final : public rhi::IPersistentCache
{
public:
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(const SlangUUID& uuid, void** object) override
    {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == rhi::IPersistentCache::getTypeGuid())
        {
            *object = static_cast<rhi::IPersistentCache*>(this);
            addRef();
            return SLANG_OK;
        }
        *object = nullptr;
        return SLANG_E_NO_INTERFACE;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++references; }

    SLANG_NO_THROW uint32_t SLANG_MCALL release() override
    {
        const auto remaining = --references;
        if (remaining == 0)
            delete this;
        return remaining;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL queryCache(ISlangBlob* key, ISlangBlob** data) override
    {
        const auto name = bytecodeKey(*key);
        const std::lock_guard lock(mutex);
        *data = nullptr;
        if (retired.contains(name))
            return SLANG_E_NOT_FOUND;

        const auto bytes = readFile(bytecodeFile(name));
        if (!bytes || bytes->empty())
            return SLANG_E_NOT_FOUND;
        *data = slang_createBlob(bytes->data(), bytes->size());
        return *data ? SLANG_OK : SLANG_FAIL;
    }

    SLANG_NO_THROW SlangResult SLANG_MCALL writeCache(ISlangBlob* key, ISlangBlob* data) override
    {
        const auto name = bytecodeKey(*key);
        const std::lock_guard lock(mutex);
        return retired.contains(name) || writeFile(bytecodeFile(name), blobBytes(*data)) ? SLANG_OK : SLANG_FAIL;
    }

    void retire(std::span<const std::string> keys)
    {
        const std::lock_guard lock(mutex);
        for (const auto& key : keys)
        {
            removeCacheFile(bytecodeFile(key));
            // A pipeline can still finish compiling on a worker after its cache has been invalidated.
            retired.insert(key);
        }
    }

    void activate(std::span<const std::string> keys)
    {
        const std::lock_guard lock(mutex);
        for (const auto& key : keys)
            retired.erase(key);
    }

private:
    std::atomic<uint32_t> references{1};
    std::mutex mutex;
    std::set<std::string, std::less<>> retired;
};

ShaderBytecodeCache& bytecodeCache()
{
    static const auto kCache = []
    {
        rhi::ComPtr<ShaderBytecodeCache> cache;
        cache.attach(new ShaderBytecodeCache);
        return cache;
    }();
    return *kCache.get();
}

std::vector<std::string> cachedBytecodeKeys(const json& header)
{
    const auto keys = header.find("bytecode");
    if (keys == header.end() || !keys->is_array())
        return {};

    std::vector<std::string> result;
    for (const auto& key : *keys)
    {
        if (!key.is_string())
            continue;
        const auto& name = key.get_ref<const std::string&>();
        if (!name.empty() && std::ranges::all_of(name, [](char digit)
            { return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'); }))
            result.push_back(name);
    }
    return result;
}

rhi::ComPtr<slang::ISession> createSourceSession(slang::IGlobalSession& globalSession, const ShaderDefines& defines)
{
    const auto source         = shaderDirectory() / "source";
    const auto shaderIncludes = (source / "shaders").string();
    const auto sharedHeaders  = (source / "src").string();
    const auto root           = source.string();
    const auto runtime        = runtimeDirectory().string();
    // Slang records the generated module source relative to the runtime directory.
    std::array searchPaths{shaderIncludes.c_str(), root.c_str(), sharedHeaders.c_str(), runtime.c_str()};

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

rhi::IPersistentCache* ShaderManager::persistentCache() { return &bytecodeCache(); }

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
    const json identity{
        {"source", key.source}, {"entries", key.entries}, {"defines", key.defines}, {"includes", key.includes},
    };
    const auto cacheName = std::format("{}-{:016x}", fs::path(key.source).stem().string(), hashIdentity(identity.dump()));
    const auto cacheFile = cacheDirectory() / "modules" / (cacheName + ".bin");
    const auto sourceFile = shaderDirectory() / "source" / key.source;
    auto compileFile = sourceFile;
    if (!key.includes.empty())
    {
        compileFile = cacheDirectory() / "modules" / (cacheName + ".slang");
        std::string input;
        for (const auto& include : key.includes)
            input.append("#include \"").append(include).append("\"\n");
        input.append("#include \"").append(key.source).append("\"\n");
        if (readFile(compileFile) != input)
            CHECK(writeFile(compileFile, input), "failed to write Slang source '{}'", compileFile.string());
    }

    rhi::ComPtr<slang::ISession> deviceSession;
    rhiCheck(device->getSlangSession(deviceSession.writeRef()));
    auto session = createSourceSession(*deviceSession->getGlobalSession(), defines);
    const auto filename = compileFile.generic_string();
    std::error_code error;
    const bool sourceExists = fs::is_regular_file(sourceFile, error);
    const auto found = variants.find(key);
    if (sourceExists && found != variants.end())
    {
        const auto& current = found->second.back();
        if (session->isBinaryModuleUpToDate(filename.c_str(), current.module))
            return current.shader;
    }

    const auto cached = readFile(cacheFile);
    json header;
    rhi::ComPtr<ISlangBlob> moduleBlob;
    rhi::ComPtr<ISlangBlob> diagnostics;
    slang::IModule* module = nullptr;
    if (cached)
    {
        const auto split = cached->find('\n');
        if (split != std::string::npos && split + 1 < cached->size())
        {
            header = json::parse(std::string_view(*cached).substr(0, split), nullptr, false);
            const auto name = header.find("module");
            if (header.contains("program") && header["program"] == identity && name != header.end() &&
                name->is_string() && sourceExists)
            {
                const auto bytes = std::string_view(*cached).substr(split + 1);
                moduleBlob.attach(slang_createBlob(bytes.data(), bytes.size()));
                if (session->isBinaryModuleUpToDate(filename.c_str(), moduleBlob))
                    module = session->loadModuleFromIRBlob(
                        name->get_ref<const std::string&>().c_str(), filename.c_str(), moduleBlob, diagnostics.writeRef()
                    );
            }
        }
    }

    const bool compiledFromSource = module == nullptr;
    if (compiledFromSource)
    {
        if (cached)
        {
            bytecodeCache().retire(cachedBytecodeKeys(header));
            removeCacheFile(cacheFile);
        }
        diagnostics.setNull();
        module = session->loadModule(filename.c_str(), diagnostics.writeRef());
    }
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
    std::vector<std::string> entryKeys;
    entryKeys.reserve(key.entries.size());
    for (size_t index = 0; index < key.entries.size(); ++index)
    {
        rhi::ComPtr<ISlangBlob> entryKey;
        linked->getEntryPointHash(static_cast<SlangInt>(index), 0, entryKey.writeRef());
        entryKeys.push_back(bytecodeKey(*entryKey.get()));
    }
    bytecodeCache().activate(entryKeys);

    rhi::ShaderProgramDesc programDesc;
    programDesc.label = key.source.c_str();
    programDesc.slangGlobalScope = linked;
    rhi::ComPtr<rhi::IShaderProgram> shader;
    rhiCheck(device->createShaderProgram(programDesc, shader.writeRef()));

    if (compiledFromSource)
    {
        rhiCheck(module->serialize(moduleBlob.writeRef()));
        std::string contents = json{{"program", identity}, {"module", module->getName()}, {"bytecode", entryKeys}}.dump();
        contents.push_back('\n');
        contents.append(blobBytes(*moduleBlob.get()));
        CHECK(writeFile(cacheFile, contents), "failed to write shader cache '{}'", cacheFile.string());
    }

    auto& versions = variants[std::move(key)];
    if (!versions.empty())
        versions.back().module.setNull();
    // Existing pipelines can still hold the previous shader and its Slang session.
    return versions.emplace_back(std::move(session), std::move(shader), std::move(moduleBlob)).shader;
}
