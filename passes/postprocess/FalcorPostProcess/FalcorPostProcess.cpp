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

#include "core/Logger.h"
#include "core/profiling/Profiler.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "core/Context.h"
#include "render/DispatchGroups.h"
#include "render/RenderContext.h"
#include "render/device/RhiCheck.h"
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

void FalcorPostProcess::initialize(const Falcor::RenderContext& context)
{
    shaders      = context.shaderManager;
    auto& device = *context.device;

    const auto create = [&](SimplePostFxVariant variant)
    {
        const auto shader = context.shaderManager->getOrLoad(
            "falcor_simple_post_fx",
            {{"FALCOR_POST_FX_STAGE", std::to_string(int(variant))}}
        );
        rhi::ComPtr<rhi::IComputePipeline> pipeline;
        rhiCheck(
            device.createComputePipeline({.program = context.shaderManager->program(shader)}, pipeline.writeRef())
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


FalcorPostProcess::FalcorPostProcess(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)),
    parameters(Falcor::deserializeFromProperties<FalcorPostProcessConfig>(properties))
{
    parameters.validate();
}


Falcor::Properties FalcorPostProcess::getProperties() const { return Falcor::serializeToProperties(parameters); }


void FalcorPostProcess::setProperties(const Falcor::Properties& properties)
{
    FalcorPostProcessConfig next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.toneMapper.autoExposure != parameters.toneMapper.autoExposure ||
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

    constexpr auto kStorageSampled =
        Falcor::ResourceBindFlags::UnorderedAccess | Falcor::ResourceBindFlags::ShaderResource;
    constexpr auto kColorSampled = Falcor::ResourceBindFlags::RenderTarget | Falcor::ResourceBindFlags::ShaderResource;
    if (parameters.simplePostFx.usesBloom())
    {
        for (uint32_t level = 0; level <= falcorPostProcess::kBloomLevels; ++level)
            reflection.addInternal(bloomImageName(level), "Bloom pyramid image")
                .texture2D(std::max(1u, extent.x >> level), std::max(1u, extent.y >> level))
                .format(rhi::Format::RGBA16Float)
                .bindFlags(kStorageSampled);
    }
    reflection.addInternal(kPostFxColor, "Color after post effects")
        .texture2D(extent.x, extent.y)
        .format(rhi::Format::RGBA32Float)
        .bindFlags(kStorageSampled);

    if (parameters.toneMapper.autoExposure)
    {
        const auto width  = std::bit_floor(std::max(1u, extent.x));
        const auto height = std::bit_floor(std::max(1u, extent.y));
        const auto levels = static_cast<uint32_t>(std::bit_width(std::max(width, height)));
        for (uint32_t level = 0; level < levels; ++level)
            reflection.addInternal(luminanceImageName(level), "Luminance pyramid image")
                .texture2D(std::max(1u, width >> level), std::max(1u, height >> level))
                .format(rhi::Format::R32Float)
                .bindFlags(kColorSampled);
    }
    reflection.addInternal(kToneMapped, "Tone-mapped color")
        .texture2D(extent.x, extent.y)
        .format(rhi::Format::RGBA32Float)
        .bindFlags(kColorSampled);
    return reflection;
}


void FalcorPostProcess::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    imageStages.clear();
    if (!downsample)
        initialize(*passContext);
    const auto targetFormat = compileData.defaultTexFormat;
    const bool srgbAttachment =
        targetFormat == rhi::Format::BGRA8UnormSrgb || targetFormat == rhi::Format::RGBA8UnormSrgb;
    CHECK(
        srgbAttachment || targetFormat == rhi::Format::BGRA8Unorm || targetFormat == rhi::Format::RGBA8Unorm,
        "unsupported Falcor presentation format: {}",
        uint32_t(targetFormat)
    );
    encodeSrgb = !srgbAttachment;
}


void FalcorPostProcess::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&      cache    = *renderCommands->gpuResources;
    auto&      commands = *renderCommands->commands;
    const auto target   = cache.registerImage(renderData.getTexture("color"));
    if (imageStages.empty())
    {
        const auto source        = cache.registerImage(renderData.getTexture("source"));
        const auto effects       = addSimplePostFx(cache, renderData, source);
        auto&      shaders       = *renderCommands->shaderManager;
        const auto toneMapped    = addToneMapper(shaders, cache, renderData, effects);
        const auto presentShader = shaders.getOrLoad("falcor_present");
        addImageStage(cache, {*this, Stage::Present, presentShader}, toneMapped, toneMapped, target);
    }

    for (auto& stage : imageStages)
        stage.execute(cache, commands, target);
}


GpuResources::ImageHandle FalcorPostProcess::addSimplePostFx(
    GpuResources& cache, const Falcor::RenderData& data, GpuResources::ImageHandle source
)
{
    auto bloomed = source;
    if (parameters.simplePostFx.usesBloom())
    {
        std::array<GpuResources::ImageHandle, falcorPostProcess::kBloomLevels + 1> pyramid;
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


GpuResources::ImageHandle FalcorPostProcess::addToneMapper(
    ShaderManager& shaders, GpuResources& cache, const Falcor::RenderData& data, GpuResources::ImageHandle source
)
{
    constexpr char kLuminanceStage[]    = "0";
    constexpr char kLuminanceMipStage[] = "1";
    const auto     extent               = cache.imageInfo(source).size;
    auto           luminance            = source;
    if (parameters.toneMapper.autoExposure)
    {
        const auto shader = shaders.getOrLoad("falcor_luminance", {{"FALCOR_LUMINANCE_STAGE", kLuminanceStage}});
        const auto target = cache.registerImage(data.getTexture(luminanceImageName(0)));
        luminance         = addImageStage(cache, {*this, Stage::Luminance, shader}, source, source, target);

        // Separate images retain the exact size, R32F quantization and linear filtering of each Falcor mip.
        const auto levels    = static_cast<uint32_t>(std::bit_width(std::max(extent.width, extent.height)));
        const auto mipShader = shaders.getOrLoad("falcor_luminance", {{"FALCOR_LUMINANCE_STAGE", kLuminanceMipStage}});
        for (uint32_t level = 1; level < levels; ++level)
        {
            const auto mip = cache.registerImage(data.getTexture(luminanceImageName(level)));
            luminance      = addImageStage(cache, {*this, Stage::Luminance, mipShader}, luminance, luminance, mip);
        }
    }

    const auto shader = shaders.getOrLoad("falcor_tonemapping");
    const auto target = cache.registerImage(data.getTexture(kToneMapped));
    return addImageStage(cache, {*this, Stage::ToneMapping, shader}, source, luminance, target);
}


GpuResources::ImageHandle FalcorPostProcess::addImageStage(
    GpuResources&             cache,
    ImageStage                pass,
    GpuResources::ImageHandle source,
    GpuResources::ImageHandle filtered,
    GpuResources::ImageHandle target
)
{
    imageStages.emplace_back(std::move(pass)).compile(cache, source, filtered, target);
    return target;
}


void FalcorPostProcess::ImageStage::compile(
    GpuResources&             cache,
    GpuResources::ImageHandle source,
    GpuResources::ImageHandle filtered,
    GpuResources::ImageHandle target
)
{
    auto& device = *owner.mpDevice;
    this->target = target;
    rhi::IShaderProgram* program;
    if (compute)
    {
        storageTarget = target;
        program       = compute->getProgram();
    }
    else
    {
        program = owner.shaders->program(shader);
        rhi::ColorTargetDesc color;
        color.format = cache.imageInfo(target).format;
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
    rhiCheck(root["sourceImage"].setBinding(cache.getImage(source)->getDefaultView()));
    if (compute)
    {
        if (stage != Stage::Downsample)
            rhiCheck(root["bloomedImage"].setBinding(cache.getImage(filtered)->getDefaultView()));
        rhiCheck(root["destinationImage"].setBinding(cache.getImage(target)->getDefaultView()));
        rhiCheck(root["linearSampler"].setBinding(owner.borderSampler));
    }
    else if (stage == Stage::ToneMapping)
    {
        rhiCheck(root["luminanceImage"].setBinding(cache.getImage(filtered)->getDefaultView()));
        rhiCheck(root["linearSampler"].setBinding(owner.linearSampler));
        rhiCheck(root["pointSampler"].setBinding(owner.pointSampler));
    }
    else if (stage == Stage::Luminance)
        rhiCheck(root["linearSampler"].setBinding(owner.linearSampler));
    else
        rhiCheck(root["pointSampler"].setBinding(owner.pointSampler));
}


void FalcorPostProcess::ImageStage::dispatch(GpuResources& cache, rhi::ICommandEncoder& commands) const
{
    const auto       extent = cache.imageExtent(storageTarget);
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


void FalcorPostProcess::ImageStage::execute(
    GpuResources& cache, rhi::ICommandEncoder& commands, GpuResources::ImageHandle colorOutput
)
{


    if (compute)
    {
        dispatch(cache, commands);
        return;
    }

    if (stage == Stage::Present)
    {
        target = colorOutput;
    }
    const auto view = cache.getImage(target)->getDefaultView();
    CHECK(view);
    rhi::RenderPassColorAttachment color;
    color.view   = view;
    color.loadOp = rhi::LoadOp::DontCare;
    auto* pass   = commands.beginRenderPass({&color, 1});
    CHECK(pass);
    pass->bindPipeline(pipeline, shaderObject);
    const auto size = cache.imageExtent(target);
    pass->setRenderState(ViewportRect{0, 0, float(size.width), float(size.height)}.renderState());
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
    recordDraw(context().profiler, 3);
    pass->end();
}
