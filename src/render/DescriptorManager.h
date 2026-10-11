#pragma once

#include "render/ShaderManager.h"

#include <compare>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

class DescriptorManager
{
public:
    void init(const vk::raii::Device& device);
    void shutdown() noexcept;

    [[nodiscard]] bool containsBinding(vk::DescriptorSetLayout layout, const ShaderMetadata::Binding& binding) const;
    [[nodiscard]] vk::DescriptorSetLayout getOrCreateLayout(std::span<const vk::DescriptorSetLayoutBinding> bindings);
    [[nodiscard]] vk::DescriptorSetLayout getOrCreateLayout(const struct ShaderMetadata& shader, uint32_t set);
    [[nodiscard]] vk::raii::DescriptorSet allocate(vk::DescriptorSetLayout layout) const;

private:
    struct BindingKey
    {
        uint32_t binding;
        uint32_t type;
        uint32_t count;
        uint32_t stages;

        [[nodiscard]] auto operator<=>(const BindingKey&) const = default;
    };

    const vk::raii::Device*                                          device = nullptr;
    vk::raii::DescriptorPool                                         pool   = nullptr;
    std::map<std::vector<BindingKey>, vk::raii::DescriptorSetLayout> layouts;
};
