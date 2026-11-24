#include "render/raytracing/RayTracingProgram.h"

#include "render/device/RhiCheck.h"

#include <utility>

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
        auto* program = shaders.getProgram(desc.source, desc.entries, defines, desc.shaderIncludes);
        desc.validateLayout(*program);

        auto pipelineDesc       = desc.pipeline;
        auto shaderTableDesc    = desc.shaderTable;
        pipelineDesc.program    = program;
        shaderTableDesc.program = program;

        Version version;
        rhiCheck(device.createRayTracingPipeline(pipelineDesc, version.pipeline.writeRef()));
        rhiCheck(device.createShaderTable(shaderTableDesc, version.shaderTable.writeRef()));
        rhiCheck(device.createRootShaderObject(version.pipeline->getProgram(), version.shaderObject.writeRef()));

        activeVersion = std::move(version);
        linkRequired  = false;
    }

    return activeVersion;
}
