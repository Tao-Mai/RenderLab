#pragma once

#include <slang-rhi.h>
#include "render/resources/ShaderManager.h"

#include <cstdint>
#include <string>
#include <string_view>

class RayTracingProgram
{
public:
    struct Desc
    {
        std::string source;
        uint32_t    maxRecursion;
        uint32_t    maxRayPayloadSize;
        uint32_t    maxAttributeSizeInBytes;
        void (*validateLayout)(rhi::IShaderProgram&);
    };

    struct Version
    {
        rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
        rhi::ComPtr<rhi::IShaderTable>        shaderTable;
        rhi::ComPtr<rhi::IShaderObject>       shaderObject;
    };

    RayTracingProgram(rhi::IDevice& device, Desc desc, ShaderDefines defines);

    void addDefine(std::string_view name, std::string_view value);
    [[nodiscard]] const Version& getActiveVersion(ShaderManager& shaders);

private:
    rhi::IDevice& device;
    Desc          desc;
    ShaderDefines defines;
    Version       activeVersion;
    bool          linkRequired = true;
};
