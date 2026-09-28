#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class GpuShader
{
  public:
    GpuShader(const vk::raii::Device &device, const std::string &filename);

    GpuShader(const GpuShader &)            = delete;
    GpuShader &operator=(const GpuShader &) = delete;
    GpuShader(GpuShader &&) noexcept        = default;
    GpuShader &operator=(GpuShader &&) noexcept = default;

    [[nodiscard]] vk::ShaderModule handle() const;

  private:
    vk::raii::ShaderModule module = nullptr;

    static std::vector<uint32_t> readSpirv(const std::string &filename);
};
