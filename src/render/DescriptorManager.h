#pragma once

#include <cstdint>
#include <compare>
#include <map>
#include <span>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

enum class DescriptorLayoutPreset
{
    Scene,
    Material,
};

class DescriptorManager
{
public:
    void init(const vk::raii::Device& device);
    void reset() noexcept;

    [[nodiscard]] vk::DescriptorSetLayout layout(DescriptorLayoutPreset preset) const;
    [[nodiscard]] bool containsBinding(DescriptorLayoutPreset preset,
        uint32_t binding, vk::DescriptorType type) const;
    [[nodiscard]] vk::DescriptorSetLayout getOrCreateLayout(
        std::span<const vk::DescriptorSetLayoutBinding> bindings);
    [[nodiscard]] vk::DescriptorSetLayout getOrCreateLayout(
        const struct ShaderMetadata& shader, uint32_t set);
    [[nodiscard]] vk::raii::DescriptorSet allocate(vk::DescriptorSetLayout layout) const;
    [[nodiscard]] vk::raii::DescriptorSet allocate(DescriptorLayoutPreset preset) const;

private:
    struct BindingKey
    {
        uint32_t binding;
        uint32_t type;
        uint32_t count;
        uint32_t stages;

        [[nodiscard]] auto operator<=>(const BindingKey&) const = default;
    };

    const vk::raii::Device* device = nullptr;
    vk::raii::DescriptorPool pool = nullptr;
    std::map<std::vector<BindingKey>, vk::raii::DescriptorSetLayout> layouts;
    vk::DescriptorSetLayout sceneLayout;
    vk::DescriptorSetLayout materialLayout;
};
