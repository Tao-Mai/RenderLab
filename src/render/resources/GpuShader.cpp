#include "render/resources/GpuShader.h"

#include "render/resources/CompiledShaderPath.h"
#include "core/RuntimeDirectory.h"
#include "render/device/VkCheck.h"

#include <fstream>
#include "core/Logger.h"

std::filesystem::path compiledShaderPath(const std::filesystem::path& path)
{
    return runtimeDirectory() / path;
}

GpuShader::GpuShader(const vk::raii::Device &device, const std::string &filename)
{
    const std::vector<uint32_t> code = readSpirv(filename);
    const vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data()
    };
    module = vkCheck(device.createShaderModule(createInfo));
}

std::vector<uint32_t> GpuShader::readSpirv(const std::string &filename)
{
    const auto path = compiledShaderPath(filename);
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    CHECK(file.is_open(), "failed to open shader: {}", path.string());

    const std::streampos end = file.tellg();
    CHECK(end > 0, "shader is empty: {}", path.string());

    const auto byteCount = static_cast<std::size_t>(end);
    CHECK(byteCount % sizeof(uint32_t) == 0, "invalid SPIR-V byte size: {}", path.string());

    std::vector<uint32_t> code(byteCount / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    CHECK(file.read(reinterpret_cast<char *>(code.data()), static_cast<std::streamsize>(byteCount)),
        "failed to read shader: {}", path.string());

    return code;
}
