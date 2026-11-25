#include "render/resources/RayTracingProgram.h"

#include "render/RhiUtils.h"
#include "render/resources/RayType.h"

#include <array>
#include <utility>
#include <vector>

namespace
{
constexpr std::array kRayGenEntries{"raygenMain"};
constexpr auto       kRayTypeCount = std::to_underlying(RayType::Count);

constexpr auto kDefaultShaderTable = []
{
    struct
    {
        std::array<rhi::HitGroupDesc, kRayTypeCount> hitGroups;
        std::array<const char*, kRayTypeCount>       hitGroupNames;
        std::array<const char*, kRayTypeCount>       missEntries;
    } table{};

    constexpr auto kScatter    = std::to_underlying(RayType::Scatter);
    constexpr auto kVisibility = std::to_underlying(RayType::Visibility);

    table.hitGroups[kScatter]    = {"scatter", "closestHitMain", "scatterAnyHitMain"};
    table.hitGroups[kVisibility] = {"visibility", nullptr, "visibilityAnyHitMain"};

    table.missEntries[kScatter]    = "missMain";
    table.missEntries[kVisibility] = "visibilityMissMain";

    for (size_t index = 0; index < table.hitGroups.size(); ++index)
        table.hitGroupNames[index] = table.hitGroups[index].hitGroupName;

    return table;
}();

const auto kShaderEntries = []
{
    std::vector<std::string> entries{kRayGenEntries.front()};
    for (const char* miss : kDefaultShaderTable.missEntries)
        entries.emplace_back(miss);

    for (const auto& group : kDefaultShaderTable.hitGroups)
    {
        if (group.closestHitEntryPoint)
            entries.emplace_back(group.closestHitEntryPoint);

        entries.emplace_back(group.anyHitEntryPoint);
    }

    return entries;
}();
}  // namespace

RayTracingProgram::RayTracingProgram(rhi::IDevice& device, Desc desc, ShaderDefines defines) :
    device(device), desc(std::move(desc)), defines(std::move(defines))
{
}

void RayTracingProgram::addDefine(std::string_view name, std::string_view value)
{
    const auto entry = defines.find(name);
    if (entry == defines.end())
        defines.emplace(name, value);
    else if (entry->second != value)
        entry->second = value;
    else
        return;

    linkRequired = true;
}

const RayTracingProgram::Version& RayTracingProgram::getActiveVersion(ShaderManager& shaders)
{
    if (linkRequired)
    {
        auto* program = shaders.getProgram(desc.source, kShaderEntries, defines);
        desc.validateLayout(*program);

        const rhi::RayTracingPipelineDesc pipelineDesc{
            .program                 = program,
            .hitGroupCount           = uint32_t(kDefaultShaderTable.hitGroups.size()),
            .hitGroups               = kDefaultShaderTable.hitGroups.data(),
            .maxRecursion            = desc.maxRecursion,
            .maxRayPayloadSize       = desc.maxRayPayloadSize,
            .maxAttributeSizeInBytes = desc.maxAttributeSizeInBytes,
        };
        const rhi::ShaderTableDesc shaderTableDesc{
            .rayGenShaderCount           = uint32_t(kRayGenEntries.size()),
            .rayGenShaderEntryPointNames = kRayGenEntries.data(),
            .missShaderCount             = uint32_t(kDefaultShaderTable.missEntries.size()),
            .missShaderEntryPointNames   = kDefaultShaderTable.missEntries.data(),
            .hitGroupCount               = uint32_t(kDefaultShaderTable.hitGroupNames.size()),
            .hitGroupNames               = kDefaultShaderTable.hitGroupNames.data(),
            .program                     = program,
        };

        Version version;
        rhiCheck(device.createRayTracingPipeline(pipelineDesc, version.pipeline.writeRef()));
        rhiCheck(device.createShaderTable(shaderTableDesc, version.shaderTable.writeRef()));
        rhiCheck(device.createRootShaderObject(version.pipeline->getProgram(), version.shaderObject.writeRef()));

        activeVersion = std::move(version);
        linkRequired  = false;
    }

    return activeVersion;
}
