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
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 4096},
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
            .binding = RenderInterface::kViewUniformBinding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kLightUniformBinding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kLightBufferBinding,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kPointShadowBufferBinding,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kRectAreaShadowBufferBinding,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kDirectionalShadowBufferBinding,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},

        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kPointShadowImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kRectAreaShadowImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kDirectionalShadowImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kShadowComparisonSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kLinearSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},

        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kIrradianceImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kIrradianceSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kPrefilteredSpecularImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kPrefilteredSpecularSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kBrdfLutImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kBrdfLutSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kRadianceImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kRadianceSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
    };
    const std::array materialBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialUniformBinding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialNormalImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialNormalSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialMetallicImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialMetallicSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialRoughnessImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialRoughnessSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialEmissiveImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialEmissiveSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialOcclusionImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialOcclusionSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialClearcoatImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialClearcoatSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialClearcoatRoughnessImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialClearcoatRoughnessSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialClearcoatNormalImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kMaterialClearcoatNormalSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
    };
    sceneLayout = getOrCreateLayout(sceneBindings);
    const std::array viewBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kViewUniformBinding,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
    };
    viewLayout = getOrCreateLayout(viewBindings);
    materialLayout = getOrCreateLayout(materialBindings);
    const std::array toneMappingBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kToneMappingImageBinding,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
        vk::DescriptorSetLayoutBinding{
            .binding = RenderInterface::kToneMappingSamplerBinding,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment},
    };
    toneMappingLayout = getOrCreateLayout(toneMappingBindings);
}

void DescriptorManager::shutdown() noexcept
{
    pool = nullptr;
    layouts.clear();
    sceneLayout = nullptr;
    viewLayout = nullptr;
    materialLayout = nullptr;
    toneMappingLayout = nullptr;
    device = nullptr;
}

vk::DescriptorSetLayout DescriptorManager::layout(DescriptorLayoutPreset preset) const
{
    switch (preset)
    {
        case DescriptorLayoutPreset::Scene: return sceneLayout;
        case DescriptorLayoutPreset::View: return viewLayout;
        case DescriptorLayoutPreset::Material: return materialLayout;
        case DescriptorLayoutPreset::ToneMapping: return toneMappingLayout;
    }
    LOG_FATAL("invalid descriptor layout preset");
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
    DCHECK(device);
    std::vector<vk::DescriptorSetLayoutBinding> sortedBindings(
        bindings.begin(), bindings.end());
    std::sort(sortedBindings.begin(), sortedBindings.end(),
        [](const auto& left, const auto& right)
        { return left.binding < right.binding; });
    DCHECK(std::adjacent_find(sortedBindings.begin(), sortedBindings.end(),
        [](const auto& left, const auto& right)
        { return left.binding == right.binding; }) == sortedBindings.end(),
        "duplicate descriptor layout binding");
    std::vector<BindingKey> key;
    key.reserve(sortedBindings.size());
    for (const auto& binding : sortedBindings)
    {
        DCHECK(!binding.pImmutableSamplers,
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
    DCHECK(device && *pool, "DescriptorManager is not initialized");
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
