#include "render/resource/shader.h"

#include "render/device/vk_check.h"

#include <fstream>
#include "logger.h"

Shader::Shader(const vk::raii::Device &device, const std::string &filename)
{
    const std::vector<uint32_t> code = readSpirv(filename);
    const vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data()
    };
    module = vkCheck(device.createShaderModule(createInfo), "vkCreateShaderModule");
}

vk::ShaderModule Shader::handle() const
{
    return *module;
}

std::vector<uint32_t> Shader::readSpirv(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    CHECK(file.is_open(), "failed to open shader: {}", filename);

    const std::streampos end = file.tellg();
    CHECK(end > 0, "shader is empty: {}", filename);

    const auto byteCount = static_cast<std::size_t>(end);
    CHECK(byteCount % sizeof(uint32_t) == 0, "invalid SPIR-V byte size: {}", filename);

    std::vector<uint32_t> code(byteCount / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    CHECK(file.read(reinterpret_cast<char *>(code.data()), static_cast<std::streamsize>(byteCount)),
        "failed to read shader: {}", filename);

    return code;
}
