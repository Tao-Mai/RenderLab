#pragma once
#include <slang-rhi.h>
#include <string_view>
#include "core/Logger.h"
class ShaderLayout
{
public:
    explicit ShaderLayout(rhi::IShaderProgram& program);
    [[nodiscard]] rhi::Format storageImageFormat(std::string_view name) const;

private:
    [[nodiscard]] slang::VariableLayoutReflection& parameter(std::string_view name) const;

    slang::ProgramLayout* reflection;
};
