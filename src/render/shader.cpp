#include "render/shader.h"

#include <fstream>
#include <stdexcept>

Shader::Shader(const vk::raii::Device &device, const std::string &filename)
{
    const std::vector<uint32_t> code = readSpirv(filename);
    const vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size() * sizeof(uint32_t),
        .pCode = code.data()
    };
    module = vk::raii::ShaderModule(device, createInfo);
}

vk::ShaderModule Shader::handle() const
{
    return *module;
}

std::vector<uint32_t> Shader::readSpirv(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("failed to open shader: " + filename);
    }

    const std::streampos end = file.tellg();
    if (end <= 0)
    {
        throw std::runtime_error("shader is empty: " + filename);
    }

    const auto byteCount = static_cast<std::size_t>(end);
    if (byteCount % sizeof(uint32_t) != 0)
    {
        throw std::runtime_error("invalid SPIR-V byte size: " + filename);
    }

    std::vector<uint32_t> code(byteCount / sizeof(uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(code.data()), static_cast<std::streamsize>(byteCount)))
    {
        throw std::runtime_error("failed to read shader: " + filename);
    }

    return code;
}
