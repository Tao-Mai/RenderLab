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
#include "passes/postprocess/FalcorPostProcess/FalcorPostProcess.h"

#include "core/Context.h"
#include "render/RenderContext.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <glm/common.hpp>
#include <glm/matrix.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <numbers>

namespace
{
constexpr auto     kPyramidFormat = vk::Format::eR16G16B16A16Sfloat;
constexpr auto     kOutputFormat  = vk::Format::eR32G32B32A32Sfloat;
constexpr uint32_t kWorkgroupSize = 16;
constexpr auto     kComputeStage  = vk::ShaderStageFlagBits::eCompute;
constexpr auto     kFragmentStage = vk::ShaderStageFlagBits::eFragment;

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

vk::ImageView imageView(RenderGraph& graph, RenderGraph::ImageHandle image)
{
    return graph.getImage(image).getOrCreateImageView({
        .viewType         = vk::ImageViewType::e2D,
        .format           = graph.imageInfo(image).format,
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
    });
}
}  // namespace

void FalcorPostProcess::initialize(const PassSetup& setup)
{
    auto&       ctx          = renderContext();
    auto&       device       = *ctx.device;
    const auto& nativeDevice = setup.vulkan.deviceHandle();
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
    computeDescriptorSetLayout  = device.createDescriptorSetLayout(computeBindings);
    graphicsDescriptorSetLayout = device.createDescriptorSetLayout(graphicsBindings);
    computePipelineLayout       = device.getOrCreatePipelineLayout(
        std::array{&computeDescriptorSetLayout},
        std::array{vk::PushConstantRange{.stageFlags = kComputeStage, .size = sizeof(PostFxParameters)}}
    );
    graphicsPipelineLayout = device.getOrCreatePipelineLayout(
        std::array{&graphicsDescriptorSetLayout},
        std::array{vk::PushConstantRange{.stageFlags = kFragmentStage, .size = sizeof(ToneMapperParameters)}}
    );
    for (uint32_t index = 0; index < computePipelines.size(); ++index)
    {
        const auto shader = ctx.shaderManager->getOrLoad("falcor_simple_post_fx", {{"FALCOR_POST_FX_STAGE", index}});
        computePipelines[index] = vkCheck(nativeDevice.createComputePipeline(
            vk::raii::PipelineCache{nullptr},
            {
                .stage  = {.stage = kComputeStage, .module = ctx.shaderManager->module(shader), .pName = "computeMain"},
                .layout = computePipelineLayout,
            }
        ));
    }

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
        const auto features = setup.vulkan.physicalDeviceHandle().getFormatProperties(format).optimalTilingFeatures;
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

FalcorPostProcess::Output FalcorPostProcess::setup(
    RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle source, RenderGraph::ImageHandle target
)
{
    imagePasses.clear();
    if (!computeDescriptorSetLayout)
        initialize(setup);
    const auto targetFormat   = graph.imageInfo(target).format;
    const bool srgbAttachment = targetFormat == vk::Format::eB8G8R8A8Srgb || targetFormat == vk::Format::eR8G8B8A8Srgb;
    CHECK(
        srgbAttachment || targetFormat == vk::Format::eB8G8R8A8Unorm || targetFormat == vk::Format::eR8G8B8A8Unorm,
        "unsupported Falcor presentation format: {}",
        vk::to_string(targetFormat)
    );
    encodeSrgb = !srgbAttachment;

    const auto add = [&](Stage                    stage,
                         RenderGraph::ImageHandle input,
                         RenderGraph::ImageHandle filtered,
                         RenderGraph::ImageHandle output,
                         uint32_t                 level = 0)
    {
        auto& pass = *imagePasses.emplace_back(std::make_unique<ImagePass>(*this, stage, level));
        return graph.addPass(pass, setup, input, filtered, output).color;
    };
    const auto createImage = [&](vk::Format format, vk::Extent3D extent)
    { return graph.createImage({.format = format, .extent = extent}); };
    const auto extent  = graph.imageInfo(source).extent;
    auto       bloomed = source;
    if (parameters.simplePostFx.usesBloom())
    {
        std::array<RenderGraph::ImageHandle, falcorPostProcess::kBloomLevels + 1> pyramid;
        for (uint32_t level = 0; level < pyramid.size(); ++level)
            pyramid[level] = createImage(
                kPyramidFormat,
                {std::max(1u, extent.width >> level), std::max(1u, extent.height >> level), 1}
            );
        for (uint32_t level = 0; level < falcorPostProcess::kBloomLevels; ++level)
        {
            const auto input   = level == 0 ? source : pyramid[level];
            pyramid[level + 1] = add(Stage::Downsample, input, input, pyramid[level + 1]);
        }
        for (uint32_t level = falcorPostProcess::kBloomLevels; level-- > 0;)
            pyramid[level] = add(Stage::Upsample, source, pyramid[level + 1], pyramid[level], level);
        bloomed = pyramid[0];
    }
    const auto postFx    = add(Stage::PostFx, source, bloomed, createImage(kOutputFormat, extent));
    auto       luminance = postFx;
    if (parameters.toneMapper.autoExposure)
    {
        vk::Extent3D mipExtent{std::bit_floor(extent.width), std::bit_floor(extent.height), 1};
        luminance = add(Stage::Luminance, postFx, postFx, createImage(vk::Format::eR32Sfloat, mipExtent));
        // Separate graph images retain the exact size, R32F quantization and linear filtering of each Falcor mip.
        while (mipExtent.width > 1 || mipExtent.height > 1)
        {
            mipExtent.width  = std::max(1u, mipExtent.width / 2);
            mipExtent.height = std::max(1u, mipExtent.height / 2);
            luminance = add(Stage::LuminanceMip, luminance, luminance, createImage(vk::Format::eR32Sfloat, mipExtent));
        }
    }
    const auto toneMapped = add(Stage::ToneMapping, postFx, luminance, createImage(kOutputFormat, extent));
    return {add(Stage::Present, toneMapped, toneMapped, target)};
}

FalcorPostProcess::Output FalcorPostProcess::ImagePass::setup(
    RenderGraph&             graph,
    const PassSetup&         setup,
    RenderGraph::ImageHandle input,
    RenderGraph::ImageHandle filteredInput,
    RenderGraph::ImageHandle target
)
{
    source             = input;
    filtered           = filteredInput;
    const auto sampled = RenderGraph::ImageAccess{
        .stages =
            isCompute() ? vk::PipelineStageFlagBits2::eComputeShader : vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    graph.use(source, sampled);
    if (filtered.id != source.id)
        graph.use(filtered, sampled);
    color = graph.write(target);
    if (isCompute())
    {
        graph.use(
            color,
            {
                .stages = vk::PipelineStageFlagBits2::eComputeShader,
                .access = vk::AccessFlagBits2::eShaderStorageWrite |
                    (stage == Stage::Upsample && level > 0 ? vk::AccessFlagBits2::eShaderStorageRead
                                                           : vk::AccessFlags2{}),
                .layout = vk::ImageLayout::eGeneral,
            }
        );
        pipeline = *owner.computePipelines[static_cast<uint32_t>(stage)];
    }
    else
    {
        graph.use(
            color,
            {
                .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .access = vk::AccessFlagBits2::eColorAttachmentWrite,
                .layout = vk::ImageLayout::eColorAttachmentOptimal,
            }
        );
        auto&      ctx    = renderContext();
        const auto shader = stage == Stage::ToneMapping ? ctx.shaderManager->getOrLoad("falcor_tonemapping")
            : stage == Stage::Present                   ? ctx.shaderManager->getOrLoad("falcor_present")
                                                        : ctx.shaderManager->getOrLoad(
                                            "falcor_luminance",
                                            {{"FALCOR_LUMINANCE_STAGE", stage == Stage::Luminance ? 0 : 1}}
                                        );
        pipeline          = ctx.device->getOrCreatePipeline({
                     .shader       = shader,
                     .layout       = owner.graphicsPipelineLayout,
                     .state        = {.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
                     .colorFormats = {graph.imageInfo(color).format},
        });
    }
    descriptorSet = renderContext().device->allocateDescriptorSet(
        isCompute() ? owner.computeDescriptorSetLayout : owner.graphicsDescriptorSetLayout
    );
    return {color};
}

void FalcorPostProcess::ImagePass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const
{
    std::array images{
        vk::DescriptorImageInfo{
            .imageView   = imageView(graph, source),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        },
        vk::DescriptorImageInfo{
            .imageView   = imageView(graph, filtered),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
        },
        isCompute()
            ? vk::DescriptorImageInfo{.imageView = imageView(graph, color), .imageLayout = vk::ImageLayout::eGeneral}
            : vk::DescriptorImageInfo{.sampler = *owner.linearSampler},
        vk::DescriptorImageInfo{.sampler = isCompute() ? *owner.borderSampler : *owner.pointSampler},
    };
    std::array<vk::WriteDescriptorSet, 4> writes;
    for (uint32_t index = 0; index < writes.size(); ++index)
        writes[index] = {
            .dstSet          = *descriptorSet,
            .dstBinding      = index,
            .descriptorCount = 1,
            .descriptorType  = index < 2     ? vk::DescriptorType::eSampledImage
                 : isCompute() && index == 2 ? vk::DescriptorType::eStorageImage
                                             : vk::DescriptorType::eSampler,
            .pImageInfo      = &images[index],
        };
    setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
}

void FalcorPostProcess::ImagePass::dispatch(
    RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame
) const
{
    const auto       extent = graph.imageExtent(color);
    const glm::uvec2 resolution{extent.width, extent.height};
    const glm::vec2  invRes{1.0f / extent.width, 1.0f / extent.height};
    const auto&      settings = owner.parameters.simplePostFx;
    if (stage == Stage::Downsample)
    {
        const DownsampleParameters parameters{resolution, invRes};
        commands.pushConstants<DownsampleParameters>(owner.computePipelineLayout, kComputeStage, 0, parameters);
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
        commands.pushConstants<UpsampleParameters>(owner.computePipelineLayout, kComputeStage, 0, parameters);
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
        commands.pushConstants<PostFxParameters>(owner.computePipelineLayout, kComputeStage, 0, parameters);
    }
    commands.dispatch(
        (extent.width + kWorkgroupSize - 1) / kWorkgroupSize,
        (extent.height + kWorkgroupSize - 1) / kWorkgroupSize,
        1
    );
}

void FalcorPostProcess::ImagePass::execute(
    RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame
) const
{
    const auto bindPoint = isCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;
    const auto layout    = isCompute() ? owner.computePipelineLayout : owner.graphicsPipelineLayout;
    commands.bindPipeline(bindPoint, pipeline);
    commands.bindDescriptorSets(bindPoint, layout, 0, std::array{*descriptorSet}, {});
    if (isCompute())
    {
        dispatch(graph, commands, frame);
        return;
    }

    const auto                        extent = graph.imageExtent(color);
    const vk::RenderingAttachmentInfo attachment{
        .imageView   = imageView(graph, color),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eDontCare,
        .storeOp     = vk::AttachmentStoreOp::eStore,
    };
    commands.beginRendering({
        .renderArea           = {{0, 0}, extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachment,
    });
    commands.setViewport(0, vk::Viewport{0, 0, float(extent.width), float(extent.height), 0, 1});
    commands.setScissor(0, vk::Rect2D{{0, 0}, extent});
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
        commands.pushConstants<ToneMapperParameters>(layout, kFragmentStage, 0, parameters);
    }
    else if (stage == Stage::Present)
    {
        const uint32_t encodeSrgb = owner.encodeSrgb ? 1u : 0u;
        commands.pushConstants<uint32_t>(layout, kFragmentStage, 0, encodeSrgb);
    }
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}
