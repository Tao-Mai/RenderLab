#include "render/DescriptorManager.h"

#include "core/Logger.h"
#include "render/device/VkCheck.h"
#include "render/resource/ShaderData.h"
#include "render/ShaderManager.h"

#include <array>
#include <algorithm>
#include <utility>

void DescriptorManager::init(const vk::raii::Device& targetDevice)
{
    device = &targetDevice;
    constexpr std::array poolSizes = {
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage, .descriptorCount = 4096},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampler, .descriptorCount = 4096},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = 4096},
    };
    const vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 4096,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    pool = vkCheck(device->createDescriptorPool(poolInfo));

    const std::array sceneBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::sceneUniformBinding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::environmentImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::environmentSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
    };
    const std::array materialBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::materialImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::materialSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::materialUniformBinding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
    };
    sceneLayout = getOrCreateLayout(sceneBindings);
    materialLayout = getOrCreateLayout(materialBindings);
}

void DescriptorManager::reset() noexcept
{
    pool = nullptr;
    layouts.clear();
    sceneLayout = nullptr;
    materialLayout = nullptr;
    device = nullptr;
}

vk::DescriptorSetLayout DescriptorManager::layout(DescriptorLayoutPreset preset) const
{
    return preset == DescriptorLayoutPreset::Scene ? sceneLayout : materialLayout;
}

bool DescriptorManager::containsBinding(DescriptorLayoutPreset preset,
    uint32_t binding, vk::DescriptorType type) const
{
    const vk::DescriptorSetLayout targetLayout = layout(preset);
    for (const auto& [key, cachedLayout] : layouts)
    {
        if (*cachedLayout == targetLayout)
        {
            return std::any_of(key.begin(), key.end(),
                [binding, type](const BindingKey& entry)
                { return entry.binding == binding &&
                    entry.type == static_cast<uint32_t>(type); });
        }
    }
    return false;
}

vk::DescriptorSetLayout DescriptorManager::getOrCreateLayout(
    std::span<const vk::DescriptorSetLayoutBinding> bindings)
{
    CHECK(device != nullptr, "DescriptorManager is not initialized");
    std::vector<vk::DescriptorSetLayoutBinding> sortedBindings(
        bindings.begin(), bindings.end());
    std::sort(sortedBindings.begin(), sortedBindings.end(),
        [](const auto& left, const auto& right)
        { return left.binding < right.binding; });
    CHECK(std::adjacent_find(sortedBindings.begin(), sortedBindings.end(),
        [](const auto& left, const auto& right)
        { return left.binding == right.binding; }) == sortedBindings.end(),
        "duplicate descriptor layout binding");
    std::vector<BindingKey> key;
    key.reserve(sortedBindings.size());
    for (const auto& binding : sortedBindings)
    {
        CHECK(binding.pImmutableSamplers == nullptr,
            "immutable samplers are not supported by DescriptorManager layout cache");
        key.push_back({binding.binding, static_cast<uint32_t>(binding.descriptorType),
            binding.descriptorCount, static_cast<uint32_t>(binding.stageFlags)});
    }
    if (const auto found = layouts.find(key); found != layouts.end())
    {
        return *found->second;
    }
    const vk::DescriptorSetLayoutCreateInfo info{
        .bindingCount = static_cast<uint32_t>(sortedBindings.size()),
        .pBindings = sortedBindings.data(),
    };
    auto result = vkCheck(device->createDescriptorSetLayout(info));
    const vk::DescriptorSetLayout handle = *result;
    layouts.emplace(std::move(key), std::move(result));
    return handle;
}

vk::DescriptorSetLayout DescriptorManager::getOrCreateLayout(
    const ShaderMetadata& shader, uint32_t set)
{
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    for (const auto& binding : shader.bindings)
    {
        if (binding.set == set)
        {
            bindings.push_back({
                .binding = binding.binding,
                .descriptorType = binding.type,
                .descriptorCount = 1,
                .stageFlags = binding.stages,
            });
        }
    }
    CHECK(!bindings.empty(), "shader '{}' has no descriptor bindings in set {}",
        shader.id, set);
    return getOrCreateLayout(bindings);
}

vk::raii::DescriptorSet DescriptorManager::allocate(vk::DescriptorSetLayout targetLayout) const
{
    CHECK(device != nullptr && *pool, "DescriptorManager is not initialized");
    const vk::DescriptorSetAllocateInfo info{
        .descriptorPool = *pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &targetLayout,
    };
    return std::move(vkCheck(device->allocateDescriptorSets(info)).front());
}

vk::raii::DescriptorSet DescriptorManager::allocate(DescriptorLayoutPreset preset) const
{
    return allocate(layout(preset));
}
