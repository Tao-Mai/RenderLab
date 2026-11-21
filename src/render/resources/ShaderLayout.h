#pragma once

#include "core/Metadata.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vulkan/vulkan_raii.hpp>

struct DescriptorSetLayout;

[[nodiscard]] inline vk::WriteDescriptorSet makeDescriptorWrite(
    vk::DescriptorSet set, const vk::DescriptorSetLayoutBinding& binding
)
{
    return {
        .dstSet = set,
        .dstBinding = binding.binding,
        .descriptorCount = binding.descriptorCount,
        .descriptorType = binding.descriptorType,
    };
}

template <vk::DescriptorType Type>
struct ShaderBinding
{
    uint32_t index = 0;

    [[nodiscard]] vk::DescriptorSetLayoutBinding layout(vk::ShaderStageFlags stages) const
    {
        return {.binding = index, .descriptorType = Type, .descriptorCount = 1, .stageFlags = stages};
    }

    [[nodiscard]] vk::WriteDescriptorSet write(vk::DescriptorSet set) const
    {
        return makeDescriptorWrite(set, layout({}));
    }
};

struct ShaderStorageImage
{
    ShaderBinding<vk::DescriptorType::eStorageImage> binding;
    vk::Format format;
};

template <class T>
struct ShaderPushConstants
{
    vk::PushConstantRange range{};

    void push(vk::raii::CommandBuffer& commands, vk::PipelineLayout layout, const T& value) const
    {
        commands.pushConstants<T>(layout, range.stageFlags, range.offset, value);
    }
};

class ShaderLayout
{
public:
    explicit ShaderLayout(const std::filesystem::path& reflection);

    template <vk::DescriptorType Type>
    [[nodiscard]] ShaderBinding<Type> binding(std::string_view name, uint32_t set) const
    {
        return {requireBinding(name, set, Type).binding};
    }

    [[nodiscard]] ShaderStorageImage storageImage(std::string_view name, uint32_t set) const;
    [[nodiscard]] vk::Extent3D threadGroupSize(std::string_view entry) const;

    template <class T>
    [[nodiscard]] ShaderBinding<vk::DescriptorType::eUniformBuffer> uniformBuffer(
        std::string_view name, uint32_t set
    ) const
    {
        const auto& parameter = requireBinding(name, set, vk::DescriptorType::eUniformBuffer);
        validateUniform<T>(name, parameter);
        return {parameter.binding};
    }

    template <class T>
    [[nodiscard]] ShaderPushConstants<T> pushConstants(std::string_view name, vk::ShaderStageFlags stages) const
    {
        const auto& parameter = requirePushConstants(name);
        validateUniform<T>(name, parameter);
        return {{.stageFlags = stages, .offset = 0, .size = parameter.uniformSize}};
    }

    void validateDescriptorSets(std::span<const DescriptorSetLayout* const> sets) const;

private:
    struct Field
    {
        uint32_t offset;
        uint32_t size;
    };

    struct Parameter
    {
        std::optional<vk::DescriptorType> descriptorType;
        uint32_t set = 0;
        uint32_t binding = 0;
        uint32_t count = 1;
        uint32_t uniformSize = 0;
        bool pushConstant = false;
        bool image2D = false;
        std::string imageFormat;
        std::map<std::string, Field, std::less<>> fields;
    };

    std::filesystem::path source;
    std::map<std::string, Parameter, std::less<>> parameters;
    std::map<std::string, vk::Extent3D, std::less<>> computeThreadGroups;

    [[nodiscard]] const Parameter& requireParameter(std::string_view name) const;
    [[nodiscard]] const Parameter& requireBinding(
        std::string_view name, uint32_t set, vk::DescriptorType type
    ) const;
    [[nodiscard]] const Parameter& requirePushConstants(std::string_view name) const;
    void requireUniformSize(std::string_view name, const Parameter& parameter, size_t size, size_t fields) const;
    void requireUniformField(std::string_view name, const Parameter& parameter,
                             std::string_view member, size_t offset, size_t size) const;

    template <class T>
    void validateUniform(std::string_view name, const Parameter& parameter) const
    {
        static_assert(std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>);
        constexpr const auto& kFields = metadata::Layout<T>::kFields;
        requireUniformSize(name, parameter, sizeof(T), kFields.size());
        for (const auto& field : kFields)
            requireUniformField(name, parameter, field.name, field.offset, field.size);
    }
};
