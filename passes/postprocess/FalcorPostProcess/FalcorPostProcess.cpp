/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "render/device/ResourceBarriers.h"
#include "render/resources/NativeFormats.h"
#include "passes/postprocess/FalcorPostProcess/FalcorPostProcess.h"

#include "core/Logger.h"
#include "core/profiling/Profiler.h"

#include "core/Context.h"
#include "render/RenderContext.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/ShaderLayout.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <format>
#include <glm/common.hpp>
#include <glm/matrix.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <numbers>
#include <utility>

namespace
{
constexpr char     kPostFxColor[] = "postFxColor";
constexpr char     kToneMapped[]  = "toneMapped";
constexpr auto     kPyramidFormat = vk::Format::eR16G16B16A16Sfloat;
constexpr auto     kOutputFormat  = vk::Format::eR32G32B32A32Sfloat;
constexpr auto     kComputeStage  = vk::ShaderStageFlagBits::eCompute;
constexpr auto     kFragmentStage = vk::ShaderStageFlagBits::eFragment;

std::string bloomImageName(uint32_t level) { return std::format("bloom{}", level); }

std::string luminanceImageName(uint32_t level) { return std::format("luminance{}", level); }

enum class SimplePostFxVariant
{
    Downsample = 0,
    Upsample = 1,
    Composite = 2,
};

struct alignas(16) DownsampleParameters
{
    glm::uvec2 resolution;
    glm::vec2  invRes;
};
struct alignas(16) UpsampleParameters
{
    glm::uvec2 resolution;
    glm::vec2  invRes;
    glm::vec2  starDir1;
    glm::vec2  starDir2;
    glm::vec2  starDir3;
    float      bloomAmount;
    float      star;
    uint32_t   inPlace;
};
struct alignas(16) PostFxParameters
{
    glm::uvec2 resolution;
    glm::vec2  invRes;
    float      saturationQuadratic;
    float      saturationLinear;
    float      saturationConstant;
    float      vignetteAmount;
    glm::vec3  colorOffset;
    float      chromaticAberrationAmount;
    glm::vec3  colorScale;
    float      barrelScale;
    glm::vec3  colorPower;
    float      barrelAmount;
    float      wipe;
    uint32_t   bypass;
};
struct alignas(16) ToneMapperParameters
{
    std::array<glm::vec4, 3> colorTransform;  // Rows of Falcor's float3x4 transform.
    float                    whiteScale;
    float                    whiteMaxLuminance;
    uint32_t                 mode;
    uint32_t                 autoExposure;
    uint32_t                 clampOutput;
};
static_assert(sizeof(DownsampleParameters) == 16);
static_assert(sizeof(UpsampleParameters) == 64);
static_assert(offsetof(UpsampleParameters, inPlace) == 48);
static_assert(sizeof(PostFxParameters) == 96);
static_assert(offsetof(PostFxParameters, bypass) == 84);
static_assert(sizeof(ToneMapperParameters) == 80);
static_assert(offsetof(ToneMapperParameters, whiteScale) == 48);
static_assert(offsetof(ToneMapperParameters, clampOutput) == 64);
}  // namespace

void FalcorPostProcess::initialize(const Falcor::RenderContext& context)
{
    auto&       device       = *context.getDevice();
    const auto& vulkan       = *context.vulkan;
    const auto& nativeDevice = vulkan.deviceHandle();
    const auto  binding      = [](uint32_t index, vk::DescriptorType type, vk::ShaderStageFlags stages)
    {
        return vk::DescriptorSetLayoutBinding{
            .binding         = index,
            .descriptorType  = type,
            .descriptorCount = 1,
            .stageFlags      = stages
        };
    };
    const std::array computeBindings{
        binding(0, vk::DescriptorType::eSampledImage, kComputeStage),
        binding(1, vk::DescriptorType::eSampledImage, kComputeStage),
        binding(2, vk::DescriptorType::eStorageImage, kComputeStage),
        binding(3, vk::DescriptorType::eSampler, kComputeStage),
    };
    const std::array graphicsBindings{
        binding(0, vk::DescriptorType::eSampledImage, kFragmentStage),
        binding(1, vk::DescriptorType::eSampledImage, kFragmentStage),
        binding(2, vk::DescriptorType::eSampler, kFragmentStage),
        binding(3, vk::DescriptorType::eSampler, kFragmentStage),
    };
    computeLayout.descriptorSet  = device.createDescriptorSetLayout(computeBindings);
    graphicsLayout.descriptorSet = device.createDescriptorSetLayout(graphicsBindings);
    computeLayout.pipeline = device.getOrCreatePipelineLayout(
        std::array{&computeLayout.descriptorSet},
        std::array{vk::PushConstantRange{.stageFlags = kComputeStage, .size = sizeof(PostFxParameters)}}
    );
    graphicsLayout.pipeline = device.getOrCreatePipelineLayout(
        std::array{&graphicsLayout.descriptorSet},
        std::array{vk::PushConstantRange{.stageFlags = kFragmentStage, .size = sizeof(ToneMapperParameters)}}
    );
    const auto createPostFxPipeline = [&](SimplePostFxVariant variant)
    {
        const auto shader =
            context.shaderManager->getOrLoad(
                "falcor_simple_post_fx", {{"FALCOR_POST_FX_STAGE", std::to_string(static_cast<int>(variant))}}
            );
        return device.createComputePipeline(shader, computeLayout.pipeline);
    };
    downsample = createPostFxPipeline(SimplePostFxVariant::Downsample);
    upsample = createPostFxPipeline(SimplePostFxVariant::Upsample);
    postFx = createPostFxPipeline(SimplePostFxVariant::Composite);

    vk::SamplerCreateInfo samplerInfo{
        .magFilter    = vk::Filter::eLinear,
        .minFilter    = vk::Filter::eLinear,
        .mipmapMode   = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToBorder,
        .addressModeV = vk::SamplerAddressMode::eClampToBorder,
        .addressModeW = vk::SamplerAddressMode::eClampToBorder,
        .maxLod       = VK_LOD_CLAMP_NONE,
        .borderColor  = vk::BorderColor::eFloatTransparentBlack,
    };
    borderSampler            = vkCheck(nativeDevice.createSampler(samplerInfo));
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    linearSampler            = vkCheck(nativeDevice.createSampler(samplerInfo));
    samplerInfo.magFilter    = vk::Filter::eNearest;
    samplerInfo.minFilter    = vk::Filter::eNearest;
    pointSampler             = vkCheck(nativeDevice.createSampler(samplerInfo));

    for (const auto format : {kPyramidFormat, kOutputFormat, vk::Format::eR32Sfloat})
    {
        const auto features = vulkan.physicalDeviceHandle().getFormatProperties(format).optimalTilingFeatures;
        auto       required = vk::FormatFeatureFlagBits::eSampledImageFilterLinear |
            (format == vk::Format::eR32Sfloat ? vk::FormatFeatureFlagBits::eColorAttachment
                                              : vk::FormatFeatureFlagBits::eStorageImage);
        if (format == kOutputFormat)
            required |= vk::FormatFeatureFlagBits::eColorAttachment;
        CHECK(
            (features & required) == required,
            "Falcor post process requires format {} features {}",
            vk::to_string(format),
            vk::to_string(required)
        );
    }
}

FalcorPostProcess::FalcorPostProcess(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<FalcorPostProcessConfig>(properties))
{
    parameters.validate();
}

Falcor::Properties FalcorPostProcess::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void FalcorPostProcess::setProperties(const Falcor::Properties& properties)
{
    FalcorPostProcessConfig next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile =
        next.toneMapper.autoExposure != parameters.toneMapper.autoExposure ||
        next.simplePostFx.usesBloom() != parameters.simplePostFx.usesBloom();
    parameters = std::move(next);
    if (recompile)
        requestRecompile();
}

Falcor::RenderPassReflection FalcorPostProcess::reflect(const CompileData& data)
{
    Falcor::RenderPassReflection reflection;
    reflection.addInput("source", "Source color").bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addOutput("color", "Post-processed output color")
        .format(data.defaultTexFormat)
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget);

    auto extent = data.defaultTexDims;
    if (const auto* input = data.connectedResources.getField("source"))
    {
        extent.x = input->getWidth() ? input->getWidth() : extent.x;
        extent.y = input->getHeight() ? input->getHeight() : extent.y;
    }

    constexpr auto kStorageSampled = Falcor::ResourceBindFlags::UnorderedAccess | Falcor::ResourceBindFlags::ShaderResource;
    constexpr auto kColorSampled = Falcor::ResourceBindFlags::RenderTarget | Falcor::ResourceBindFlags::ShaderResource;
    if (parameters.simplePostFx.usesBloom())
    {
        for (uint32_t level = 0; level <= falcorPostProcess::kBloomLevels; ++level)
            reflection.addInternal(bloomImageName(level), "Bloom pyramid image")
                .texture2D(std::max(1u, extent.x >> level), std::max(1u, extent.y >> level))
                .format(Falcor::ResourceFormat::RGBA16Float)
                .bindFlags(kStorageSampled);
    }
    reflection.addInternal(kPostFxColor, "Color after post effects")
        .texture2D(extent.x, extent.y)
        .format(Falcor::ResourceFormat::RGBA32Float)
        .bindFlags(kStorageSampled);

    if (parameters.toneMapper.autoExposure)
    {
        const auto width = std::bit_floor(std::max(1u, extent.x));
        const auto height = std::bit_floor(std::max(1u, extent.y));
        const auto levels = static_cast<uint32_t>(std::bit_width(std::max(width, height)));
        for (uint32_t level = 0; level < levels; ++level)
            reflection.addInternal(luminanceImageName(level), "Luminance pyramid image")
                .texture2D(std::max(1u, width >> level), std::max(1u, height >> level))
                .format(Falcor::ResourceFormat::R32Float)
                .bindFlags(kColorSampled);
    }
    reflection.addInternal(kToneMapped, "Tone-mapped color")
        .texture2D(extent.x, extent.y)
        .format(Falcor::ResourceFormat::RGBA32Float)
        .bindFlags(kColorSampled);
    return reflection;
}

void FalcorPostProcess::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    imageStages.clear();
    if (!computeLayout.descriptorSet)
        initialize(*passContext);
    const auto targetFormat   = Falcor::getVulkanFormat(compileData.defaultTexFormat);
    const bool srgbAttachment = targetFormat == vk::Format::eB8G8R8A8Srgb || targetFormat == vk::Format::eR8G8B8A8Srgb;
    CHECK(
        srgbAttachment || targetFormat == vk::Format::eB8G8R8A8Unorm || targetFormat == vk::Format::eR8G8B8A8Unorm,
        "unsupported Falcor presentation format: {}",
        vk::to_string(targetFormat)
    );
    encodeSrgb = !srgbAttachment;
}

void FalcorPostProcess::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache    = *renderCommands->vulkanResources;
    auto&       commands = *renderCommands->commands;
    const auto target = cache.registerImage(renderData.getTexture("color"));
    if (imageStages.empty())
    {
        const auto source = cache.registerImage(renderData.getTexture("source"));
        const auto effects = addSimplePostFx(cache, renderData, source);
        auto& shaders = *renderCommands->shaderManager;
        const auto toneMapped = addToneMapper(shaders, cache, renderData, effects);
        const auto presentShader = shaders.getOrLoad("falcor_present");
        addImageStage(cache, {*this, Stage::Present, presentShader}, toneMapped, toneMapped, target);
    }

    for (auto& stage : imageStages)
        stage.execute(cache, commands, target);
}

VulkanResources::ImageHandle FalcorPostProcess::addSimplePostFx(
    VulkanResources& cache, const Falcor::RenderData& data, VulkanResources::ImageHandle source
)
{
    auto bloomed = source;
    if (parameters.simplePostFx.usesBloom())
    {
        std::array<VulkanResources::ImageHandle, falcorPostProcess::kBloomLevels + 1> pyramid;
        for (uint32_t level = 0; level < pyramid.size(); ++level)
            pyramid[level] = cache.registerImage(data.getTexture(bloomImageName(level)));
        for (uint32_t level = 0; level < falcorPostProcess::kBloomLevels; ++level)
        {
            const auto input = level == 0 ? source : pyramid[level];
            addImageStage(cache, {*this, Stage::Downsample, downsample}, input, input, pyramid[level + 1]);
        }
        for (uint32_t level = falcorPostProcess::kBloomLevels; level-- > 0;)
            addImageStage(cache, {*this, Stage::Upsample, upsample, level}, source, pyramid[level + 1], pyramid[level]);
        bloomed = pyramid[0];
    }

    const auto target = cache.registerImage(data.getTexture(kPostFxColor));
    return addImageStage(cache, {*this, Stage::PostFx, postFx}, source, bloomed, target);
}

VulkanResources::ImageHandle FalcorPostProcess::addToneMapper(
    ShaderManager& shaders, VulkanResources& cache, const Falcor::RenderData& data, VulkanResources::ImageHandle source
)
{
    constexpr char kLuminanceStage[] = "0";
    constexpr char kLuminanceMipStage[] = "1";
    const auto    extent             = cache.imageInfo(source).extent;
    auto luminance = source;
    if (parameters.toneMapper.autoExposure)
    {
        const auto shader = shaders.getOrLoad("falcor_luminance", {{"FALCOR_LUMINANCE_STAGE", kLuminanceStage}});
        const auto target = cache.registerImage(data.getTexture(luminanceImageName(0)));
        luminance = addImageStage(cache, {*this, Stage::Luminance, shader}, source, source, target);

        // Separate images retain the exact size, R32F quantization and linear filtering of each Falcor mip.
        const auto levels = static_cast<uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
        const auto mipShader =
            shaders.getOrLoad("falcor_luminance", {{"FALCOR_LUMINANCE_STAGE", kLuminanceMipStage}});
        for (uint32_t level = 1; level < levels; ++level)
        {
            const auto mip = cache.registerImage(data.getTexture(luminanceImageName(level)));
            luminance = addImageStage(cache, {*this, Stage::Luminance, mipShader}, luminance, luminance, mip);
        }
    }

    const auto shader = shaders.getOrLoad("falcor_tonemapping");
    const auto target = cache.registerImage(data.getTexture(kToneMapped));
    return addImageStage(cache, {*this, Stage::ToneMapping, shader}, source, luminance, target);
}

VulkanResources::ImageHandle FalcorPostProcess::addImageStage(
    VulkanResources&             cache,
    ImageStage                 pass,
    VulkanResources::ImageHandle source,
    VulkanResources::ImageHandle filtered,
    VulkanResources::ImageHandle target
)
{
    imageStages.emplace_back(std::move(pass)).compile(cache, source, filtered, target);
    return target;
}

void FalcorPostProcess::ImageStage::compile(
    VulkanResources&             cache,
    VulkanResources::ImageHandle source,
    VulkanResources::ImageHandle filtered,
    VulkanResources::ImageHandle target
)
{
    const auto sampled = VulkanResources::ImageAccess{
        .stages = compute ? vk::PipelineStageFlagBits2::eComputeShader : vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    accesses.add(source, sampled);
    if (filtered != source)
        accesses.add(filtered, sampled);
    auto& device = *owner.mpDevice;
    if (compute)
    {
        storageTarget = target;
        accesses.add(
            target,
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageWrite |
                    (stage == Stage::Upsample && level > 0 ? vk::AccessFlagBits2::eShaderStorageRead
                                                           : vk::AccessFlags2{}),
                .layout = vk::ImageLayout::eGeneral,
            }
        );
        pipeline = **compute;
    }
    else
    {
        attachments.colors.front() = {target, vk::AttachmentLoadOp::eDontCare};
        if (stage != Stage::Present)
            accesses.add(target, vulkan_access::kColorWrite);
        pipeline = device.getOrCreatePipeline({
            .shader       = shader,
            .layout       = layout.pipeline,
            .state        = {.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
            .colorFormats = {cache.imageInfo(target).format},
        });
    }
    accesses.declare(cache);
    descriptorSet = device.allocateDescriptorSet(layout.descriptorSet);

    const auto& bindings = layout.descriptorSet.bindings;
    cache.writeDescriptor(*descriptorSet, bindings[0].binding, source);
    cache.writeDescriptor(*descriptorSet, bindings[1].binding, filtered);
    if (compute)
        cache.writeDescriptor(*descriptorSet, bindings[2].binding, storageTarget, vk::DescriptorType::eStorageImage);
    else
        cache.writeDescriptor(*descriptorSet, bindings[2].binding, *owner.linearSampler);
    cache.writeDescriptor(*descriptorSet, bindings[3].binding, compute ? *owner.borderSampler : *owner.pointSampler);
}

void FalcorPostProcess::ImageStage::dispatch(VulkanResources& cache, vk::raii::CommandBuffer& commands) const
{
    const auto       extent = cache.imageExtent(storageTarget);
    const glm::uvec2 resolution{extent.width, extent.height};
    const glm::vec2  invRes{1.0f / extent.width, 1.0f / extent.height};
    const auto&      settings = owner.parameters.simplePostFx;
    if (stage == Stage::Downsample)
    {
        const DownsampleParameters parameters{resolution, invRes};
        commands.pushConstants<DownsampleParameters>(layout.pipeline, kComputeStage, 0, parameters);
    }
    else if (stage == Stage::Upsample)
    {
        const bool wantStar  = level == 1 || level == 2;
        const auto direction = [&](float angle) { return glm::vec2{std::sin(angle), std::cos(angle)} * invRes * 2.0f; };
        float      angle     = settings.starAngle;
        const auto starDir1  = direction(angle);
        angle += std::numbers::pi_v<float> / 3.0f;
        const auto starDir2 = direction(angle);
        angle += std::numbers::pi_v<float> / 3.0f;
        const UpsampleParameters parameters{
            .resolution  = resolution,
            .invRes      = invRes,
            .starDir1    = starDir1,
            .starDir2    = starDir2,
            .starDir3    = direction(angle),
            .bloomAmount = settings.bloomAmount,
            .star        = wantStar ? settings.starAmount : 0.0f,
            .inPlace     = level > 0 ? 1u : 0u,
        };
        commands.pushConstants<UpsampleParameters>(layout.pipeline, kComputeStage, 0, parameters);
    }
    else
    {
        auto curve = settings.saturationCurve;
        curve.y -= curve.x;
        curve.z -= curve.x;
        const float            quadratic = 2.0f * curve.z - 4.0f * curve.y;
        const float            linear    = curve.z - quadratic;
        const float            barrel    = settings.barrelDistortAmount * 0.125f;
        const PostFxParameters parameters{
            .resolution                = resolution,
            .invRes                    = invRes,
            .saturationQuadratic       = quadratic,
            .saturationLinear          = linear,
            .saturationConstant        = curve.x,
            .vignetteAmount            = settings.vignetteAmount,
            .colorOffset               = settings.colorOffset + settings.colorOffsetScalar - 0.5f,
            .chromaticAberrationAmount = settings.chromaticAberrationAmount * (1.0f / 64.0f),
            .colorScale                = settings.colorScale * std::exp2(1.0f + 2.0f * settings.colorScaleScalar),
            .barrelScale               = 1.0f / (1.0f + 4.0f * barrel),
            .colorPower                = glm::exp2(3.0f * (0.5f - settings.colorPower - settings.colorPowerScalar)),
            .barrelAmount              = barrel,
            .wipe                      = settings.wipe * extent.width,
            .bypass                    = settings.bypass() ? 1u : 0u,
        };
        commands.pushConstants<PostFxParameters>(layout.pipeline, kComputeStage, 0, parameters);
    }
    compute->dispatch(commands, {extent.width, extent.height, 1});
}

void FalcorPostProcess::ImageStage::execute(
    VulkanResources& cache, vk::raii::CommandBuffer& commands, VulkanResources::ImageHandle colorOutput
)
{
    accesses.transition(cache, commands);
    const auto bindPoint = compute ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;
    commands.bindPipeline(bindPoint, pipeline);
    commands.bindDescriptorSets(bindPoint, layout.pipeline, 0, std::array{*descriptorSet}, {});
    if (compute)
    {
        dispatch(cache, commands);
        return;
    }

    if (stage == Stage::Present)
    {
        attachments.colors.front().image = colorOutput;
        cache.transition(commands, colorOutput, vulkan_access::kColorWrite);
    }
    attachments.begin(cache, commands);
    if (stage == Stage::ToneMapping)
    {
        const auto&                settings  = owner.parameters.toneMapper;
        const auto                 transform = glm::transpose(settings.colorTransform());
        const ToneMapperParameters parameters{
            .colorTransform    = {glm::vec4{transform[0], 0}, glm::vec4{transform[1], 0}, glm::vec4{transform[2], 0}},
            .whiteScale        = settings.whiteScale,
            .whiteMaxLuminance = settings.whiteMaxLuminance,
            .mode              = static_cast<uint32_t>(settings.mode),
            .autoExposure      = settings.autoExposure ? 1u : 0u,
            .clampOutput       = settings.clampOutput ? 1u : 0u,
        };
        commands.pushConstants<ToneMapperParameters>(layout.pipeline, kFragmentStage, 0, parameters);
    }
    else if (stage == Stage::Present)
    {
        const uint32_t encodeSrgb = owner.encodeSrgb ? 1u : 0u;
        commands.pushConstants<uint32_t>(layout.pipeline, kFragmentStage, 0, encodeSrgb);
    }
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}
