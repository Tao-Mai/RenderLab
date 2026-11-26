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
#include "render/graph/RenderPassSettings.h"

#include "render/impl/postprocess/FalcorPostProcess/FalcorPostProcess.h"

#include "core/Logger.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/DispatchGroups.h"
#include "render/RenderContext.h"
#include "render/RhiUtils.h"

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
constexpr char kPostFxColor[] = "postFxColor";
constexpr char kToneMapped[]  = "toneMapped";
constexpr auto kPyramidFormat = rhi::Format::RGBA16Float;
constexpr auto kOutputFormat  = rhi::Format::RGBA32Float;

std::string bloomImageName(uint32_t level) { return std::format("bloom{}", level); }

std::string luminanceImageName(uint32_t level) { return std::format("luminance{}", level); }

enum class SimplePostFxVariant
{
    Downsample = 0,
    Upsample   = 1,
    Composite  = 2,
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

void FalcorPostProcess::initialize(const RenderContext& context)
{
    auto& device = *mpDevice;

    const auto create = [&](SimplePostFxVariant variant)
    {
        const auto shader = context.shaderManager->getProgram(
            "postprocess/FalcorPostProcess/simple_post_fx.slang", {"computeMain"},
            {{"POST_FX_STAGE", std::to_string(int(variant))}}
        );
        rhi::ComPtr<rhi::IComputePipeline> pipeline;
        rhiCheck(
            device.createComputePipeline({.program = shader}, pipeline.writeRef())
        );
        return pipeline;
    };
    downsample = create(SimplePostFxVariant::Downsample);
    upsample   = create(SimplePostFxVariant::Upsample);
    postFx     = create(SimplePostFxVariant::Composite);
    rhi::SamplerDesc desc;
    desc.minFilter = desc.magFilter = rhi::TextureFilteringMode::Linear;
    desc.mipFilter                  = rhi::TextureFilteringMode::Point;
    desc.addressU = desc.addressV = desc.addressW = rhi::TextureAddressingMode::ClampToBorder;
    rhiCheck(device.createSampler(desc, borderSampler.writeRef()));
    desc.addressU = desc.addressV = desc.addressW = rhi::TextureAddressingMode::Wrap;
    rhiCheck(device.createSampler(desc, linearSampler.writeRef()));
    desc.minFilter = desc.magFilter = rhi::TextureFilteringMode::Point;
    rhiCheck(device.createSampler(desc, pointSampler.writeRef()));
}


FalcorPostProcess::FalcorPostProcess(rhi::ComPtr<rhi::IDevice> device) :
    RenderPass(
        std::move(device),
        {
            .type = kType,
            .ports = kPorts,
            .settings = pass_settings::bind<
                &FalcorPostProcess::parameters, &RenderPassOptions::falcor,
                &FalcorPostProcess::drawSettings, &FalcorPostProcess::onSettingsUpdated
            >(),
        }
    ),
    parameters{}
{
    parameters.validate();
}

void FalcorPostProcess::onSettingsUpdated(const FalcorPostProcessConfig& previous)
{
    if (parameters.toneMapper.autoExposure != previous.toneMapper.autoExposure ||
        parameters.simplePostFx.usesBloom() != previous.simplePostFx.usesBloom())
        requestRecompile();
}

const RenderPassReflection::Field FalcorPostProcess::kPorts[]{
    {
        .name = "source",
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource},
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {
        .name = "color",
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::RenderTarget},
        .visibility = RenderPassReflection::Visibility::Output,
        .textureMatch = RenderPassReflection::TextureMatch::Format,
    },
};


void FalcorPostProcess::compile(RenderContext* passContext, const CompileData& compileData)
{
    imageStages.clear();
    if (!downsample)
        initialize(*passContext);

    const auto targetFormat = compileData.getTextureDesc("color").format;
    const bool srgbAttachment =
        targetFormat == rhi::Format::BGRA8UnormSrgb || targetFormat == rhi::Format::RGBA8UnormSrgb;
    CHECK(
        srgbAttachment || targetFormat == rhi::Format::BGRA8Unorm || targetFormat == rhi::Format::RGBA8Unorm,
        "unsupported Falcor presentation format: {}",
        uint32_t(targetFormat)
    );
    encodeSrgb = !srgbAttachment;

    const auto extent = compileData.getTextureDesc("source").size;
    const auto kStorageSampled = rhi::TextureUsage::UnorderedAccess | rhi::TextureUsage::ShaderResource;
    const auto kColorSampled   = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::ShaderResource;
    if (parameters.simplePostFx.usesBloom())
    {
        for (uint32_t level = 0; level <= falcorPostProcess::kBloomLevels; ++level)
            compileData.addInternalTexture(bloomImageName(level), {
                .size = {std::max(1u, extent.width >> level), std::max(1u, extent.height >> level), 1},
                .format = rhi::Format::RGBA16Float,
                .usage = kStorageSampled,
            });
    }
    compileData.addInternalTexture(kPostFxColor, {
        .size = {extent.width, extent.height, 1},
        .format = rhi::Format::RGBA32Float,
        .usage = kStorageSampled,
    });

    if (parameters.toneMapper.autoExposure)
    {
        const auto width  = std::bit_floor(std::max(1u, extent.width));
        const auto height = std::bit_floor(std::max(1u, extent.height));
        const auto levels = static_cast<uint32_t>(std::bit_width(std::max(width, height)));
        for (uint32_t level = 0; level < levels; ++level)
            compileData.addInternalTexture(luminanceImageName(level), {
                .size = {std::max(1u, width >> level), std::max(1u, height >> level), 1},
                .format = rhi::Format::R32Float,
                .usage = kColorSampled,
            });
    }
    compileData.addInternalTexture(kToneMapped, {
        .size = {extent.width, extent.height, 1},
        .format = rhi::Format::RGBA32Float,
        .usage = kColorSampled,
    });
}


void FalcorPostProcess::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    auto&      commands = *renderCommands->commands;
    const auto target   = renderData.getTexture("color");
    if (imageStages.empty())
    {
        const auto source        = renderData.getTexture("source");
        const auto effects       = addSimplePostFx(renderData, source);
        auto&      shaders       = *renderCommands->shaderManager;
        const auto toneMapped    = addToneMapper(shaders, renderData, effects);
        const auto presentShader =
            shaders.getProgram("postprocess/blit.slang", {"vertMain", "fragMain"}, {{"BLIT_SRGB_CONTROL", "1"}});
        addImageStage({*this, Stage::Present, presentShader}, toneMapped, toneMapped, target);
    }

    for (const auto& stage : imageStages)
        stage.execute(commands, target);
}


rhi::ITexture* FalcorPostProcess::addSimplePostFx(const RenderData& data, rhi::ITexture* source)
{
    auto bloomed = source;
    if (parameters.simplePostFx.usesBloom())
    {
        std::array<rhi::ITexture*, falcorPostProcess::kBloomLevels + 1> pyramid;
        for (uint32_t level = 0; level < pyramid.size(); ++level)
            pyramid[level] = data.getTexture(bloomImageName(level));
        for (uint32_t level = 0; level < falcorPostProcess::kBloomLevels; ++level)
        {
            const auto input = level == 0 ? source : pyramid[level];
            addImageStage({*this, Stage::Downsample, downsample}, input, input, pyramid[level + 1]);
        }
        for (uint32_t level = falcorPostProcess::kBloomLevels; level-- > 0;)
            addImageStage({*this, Stage::Upsample, upsample, level}, source, pyramid[level + 1], pyramid[level]);
        bloomed = pyramid[0];
    }

    return addImageStage({*this, Stage::PostFx, postFx}, source, bloomed, data.getTexture(kPostFxColor));
}


rhi::ITexture* FalcorPostProcess::addToneMapper(ShaderManager& shaders, const RenderData& data, rhi::ITexture* source)
{
    constexpr char kLuminanceStage[]    = "0";
    constexpr char kLuminanceMipStage[] = "1";
    const auto     extent               = source->getDesc().size;
    auto           luminance            = source;
    if (parameters.toneMapper.autoExposure)
    {
        const auto shader = shaders.getProgram(
            "postprocess/FalcorPostProcess/luminance.slang",
            {"vertMain", "fragMain"},
            {{"LUMINANCE_STAGE", kLuminanceStage}}
        );
        luminance =
            addImageStage({*this, Stage::Luminance, shader}, source, source, data.getTexture(luminanceImageName(0)));

        // Separate images retain the exact size, R32F quantization and linear filtering of each Falcor mip.
        const auto levels    = static_cast<uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
        const auto mipShader = shaders.getProgram(
            "postprocess/FalcorPostProcess/luminance.slang",
            {"vertMain", "fragMain"},
            {{"LUMINANCE_STAGE", kLuminanceMipStage}}
        );
        for (uint32_t level = 1; level < levels; ++level)
        {
            luminance = addImageStage(
                {*this, Stage::Luminance, mipShader},
                luminance,
                luminance,
                data.getTexture(luminanceImageName(level))
            );
        }
    }

    const auto shader =
        shaders.getProgram("postprocess/FalcorPostProcess/tonemapping.slang", {"vertMain", "fragMain"});
    return addImageStage({*this, Stage::ToneMapping, shader}, source, luminance, data.getTexture(kToneMapped));
}


rhi::ITexture*
FalcorPostProcess::addImageStage(ImageStage pass, rhi::ITexture* source, rhi::ITexture* filtered, rhi::ITexture* target)
{
    imageStages.emplace_back(std::move(pass)).compile(source, filtered, target);
    return target;
}


void FalcorPostProcess::ImageStage::compile(rhi::ITexture* source, rhi::ITexture* filtered, rhi::ITexture* target)
{
    auto& device = *owner.mpDevice;
    this->target = target;
    rhi::IShaderProgram* program;
    if (compute)
        program = compute->getProgram();
    else
    {
        program = shader;
        rhi::ColorTargetDesc color;
        color.format = target->getDesc().format;
        rhi::RenderPipelineDesc desc;
        desc.program                       = program;
        desc.targets                       = &color;
        desc.targetCount                   = 1;
        desc.depthStencil.depthTestEnable  = false;
        desc.depthStencil.depthWriteEnable = false;
        desc.rasterizer.scissorEnable      = true;
        rhiCheck(device.createRenderPipeline(desc, pipeline.writeRef()));
    }
    rhiCheck(device.createRootShaderObject(program, shaderObject.writeRef()));
    const rhi::ShaderCursor root(shaderObject);
    rhiCheck(root["sourceImage"].setBinding(source));
    if (compute)
    {
        if (stage != Stage::Downsample)
            rhiCheck(root["bloomedImage"].setBinding(filtered));
        rhiCheck(root["destinationImage"].setBinding(target));
        rhiCheck(root["linearSampler"].setBinding(owner.borderSampler));
    }
    else if (stage == Stage::ToneMapping)
    {
        rhiCheck(root["luminanceImage"].setBinding(filtered));
        rhiCheck(root["linearSampler"].setBinding(owner.linearSampler));
        rhiCheck(root["pointSampler"].setBinding(owner.pointSampler));
    }
    else if (stage == Stage::Luminance)
        rhiCheck(root["linearSampler"].setBinding(owner.linearSampler));
    else
        rhiCheck(root["pointSampler"].setBinding(owner.pointSampler));
}


void FalcorPostProcess::ImageStage::dispatch(rhi::ICommandEncoder& commands) const
{
    const auto       extent = target->getDesc().size;
    const glm::uvec2 resolution{extent.width, extent.height};
    const glm::vec2  invRes{1.0f / extent.width, 1.0f / extent.height};
    const auto&      settings = owner.parameters.simplePostFx;
    if (stage == Stage::Downsample)
    {
        const DownsampleParameters parameters{resolution, invRes};
        {
            const auto uniformValue  = parameters;
            const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
            CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
            rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
        };
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
        {
            const auto uniformValue  = parameters;
            const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
            CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
            rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
        };
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
        {
            const auto uniformValue  = parameters;
            const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
            CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
            rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
        };
    }
    auto* pass = commands.beginComputePass();
    CHECK(pass);
    pass->bindPipeline(compute, shaderObject);
    const auto groups = dispatchGroups(compute, {extent.width, extent.height, 1});
    pass->dispatchCompute(groups.width, groups.height, groups.depth);
    pass->end();
}


void FalcorPostProcess::ImageStage::execute(rhi::ICommandEncoder& commands, rhi::ITexture* colorOutput) const
{
    if (compute)
    {
        dispatch(commands);
        return;
    }

    auto*      output = stage == Stage::Present ? colorOutput : target.get();
    const auto view   = output->getDefaultView();
    CHECK(view);
    rhi::RenderPassColorAttachment color;
    color.view   = view;
    color.loadOp = rhi::LoadOp::DontCare;
    auto* pass   = commands.beginRenderPass({&color, 1});
    CHECK(pass);
    pass->bindPipeline(pipeline, shaderObject);
    pass->setRenderState(fullscreenState(output->getDesc().size));
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
        {
            const auto uniformValue  = parameters;
            const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
            CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
            rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
        };
    }
    else if (stage == Stage::Present)
    {
        const uint32_t encodeSrgb = owner.encodeSrgb ? 1u : 0u;
        {
            const auto uniformValue  = encodeSrgb;
            const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
            CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
            rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
        };
    }
    pass->draw({3});
    pass->end();
}
