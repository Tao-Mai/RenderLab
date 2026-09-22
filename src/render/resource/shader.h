#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class Shader
{
  public:
    Shader(const vk::raii::Device &device, const std::string &filename);

    Shader(const Shader &)            = delete;
    Shader &operator=(const Shader &) = delete;
    Shader(Shader &&) noexcept        = default;
    Shader &operator=(Shader &&) noexcept = default;

    [[nodiscard]] vk::ShaderModule handle() const;

  private:
    vk::raii::ShaderModule module = nullptr;

    static std::vector<uint32_t> readSpirv(const std::string &filename);
};
