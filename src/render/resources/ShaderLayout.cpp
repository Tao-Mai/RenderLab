#include "render/resources/ShaderLayout.h"

ShaderLayout::ShaderLayout(rhi::IShaderProgram& program) : reflection(program.getSlangComponentType()->getLayout())
{
    CHECK(reflection);
}

slang::VariableLayoutReflection& ShaderLayout::parameter(std::string_view name) const
{
    for (unsigned index = 0; index < reflection->getParameterCount(); ++index)
    {
        auto* variable = reflection->getParameterByIndex(index);
        if (const auto* parameterName = variable->getName(); parameterName && name == parameterName)
            return *variable;
    }
    LOG_FATAL("shader parameter '{}' is missing", name);
}

rhi::Format ShaderLayout::storageImageFormat(std::string_view name) const
{
    const auto format = parameter(name).getImageFormat();
    switch (format)
    {
        case SLANG_IMAGE_FORMAT_r32f: return rhi::Format::R32Float;
        case SLANG_IMAGE_FORMAT_r32ui: return rhi::Format::R32Uint;
        case SLANG_IMAGE_FORMAT_rg16f: return rhi::Format::RG16Float;
        case SLANG_IMAGE_FORMAT_rgba16f: return rhi::Format::RGBA16Float;
        case SLANG_IMAGE_FORMAT_rgba32f: return rhi::Format::RGBA32Float;
        case SLANG_IMAGE_FORMAT_rgba8: return rhi::Format::RGBA8Unorm;
        default: LOG_FATAL("unsupported storage image format {} for '{}'", int(format), name);
    }
}
