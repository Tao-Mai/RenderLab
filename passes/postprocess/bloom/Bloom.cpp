#include "passes/postprocess/Bloom/Bloom.h"

#include "core/Logger.h"
#include "core/Serializer.h"

#include <slang-rhi.h>
#include "asset/BuiltinAssets.h"
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/resources/RenderResourceManager.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <glm/vec4.hpp>

namespace
{
constexpr std::string_view kHorizontal = "horizontal";
constexpr std::string_view kVertical   = "vertical";
constexpr std::string_view kUpsample   = "upsample";
constexpr char             kHdrColor[] = "hdrColor";
constexpr char             kOutputColor[] = "color";

struct alignas(16) BloomParameters
{
    glm::vec4 viewport;  // Normalized min/max, identical across the image pyramid.
    glm::vec2 texelSize;
    float     filterRadius;
    float     intensity;
    float     scatter;
};
static_assert(sizeof(BloomParameters) == 48);

uint32_t pyramidLevels(glm::uvec2 extent, uint32_t limit)
{
    const auto levels = std::bit_width(std::max({2u, extent.x, extent.y})) - 1;
    return std::min(static_cast<uint32_t>(levels), limit);
}

std::string pyramidImageName(std::string_view stage, uint32_t level) { return std::format("{}{}", stage, level); }
}  // namespace

void BloomSettings::validate() const
{
    CHECK(std::isfinite(intensity) && intensity >= 0.0f && intensity <= 1.0f);
    CHECK(std::isfinite(scatter) && scatter >= 0.05f && scatter <= 0.95f);
    CHECK(std::isfinite(filterRadius) && filterRadius > 0.0f);
    CHECK(levels > 0 && levels <= kMaxBloomLevels);
}


Bloom::Bloom(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
    RenderPass(std::move(device)), parameters(DeserializeWithDefaults<BloomSettings>(properties))
{
    parameters.validate();
}

void Bloom::onSettingsUpdated(const BloomSettings& previous)
{
    if (parameters.levels != previous.levels)
        requestRecompile();
}

const RenderPassReflection::Field Bloom::kPorts[]{
    {
        .name = kHdrColor,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::ShaderResource},
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {
        .name = kOutputColor,
        .resource = rhi::TextureDesc{.size = {0, 0, 1}, .usage = rhi::TextureUsage::RenderTarget},
        .visibility = RenderPassReflection::Visibility::Output,
        .matchInput = kHdrColor,
        .textureMatch = RenderPassReflection::TextureMatch::Extent | RenderPassReflection::TextureMatch::Format,
    },
};


void Bloom::compile(RenderContext*, const CompileData& compileData)
{
    const auto input = compileData.getTextureDesc(kHdrColor);
    const auto format = input.format;
    if (format != rhi::Format::RGBA16Float && format != rhi::Format::RGBA32Float)
    {
        LOG_ERROR("Bloom requires RGBA16Float or RGBA32Float for 'hdrColor'");
        return;
    }

    rhi::FormatSupport support;
    rhiCheck(mpDevice->getFormatSupport(format, &support));
    const auto kRequiredSupport = rhi::FormatSupport::ShaderSample | rhi::FormatSupport::RenderTarget;
    if ((support & kRequiredSupport) != kRequiredSupport)
    {
        LOG_ERROR("Bloom requires linear sampling and render-target support for format {}", int(format));
        return;
    }

    const glm::uvec2 extent{input.size.width, input.size.height};
    const auto levels   = pyramidLevels(extent, parameters.levels);
    auto       size     = extent;
    const auto addImage = [&](std::string_view stage, uint32_t level)
    {
        compileData.addInternalTexture(pyramidImageName(stage, level), {
            .size = {size.x, size.y, 1},
            .format = format,
            .usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::RenderTarget,
        });
    };
    for (uint32_t level = 0; level < levels; ++level)
    {
        size = {std::max(1u, size.x / 2), std::max(1u, size.y / 2)};
        addImage(kHorizontal, level);
        addImage(kVertical, level);
        if (level + 1 < levels)
            addImage(kUpsample, level);
    }
    filters.clear();
}


void Bloom::prepareFilters(const RenderContext& context, const RenderData& renderData)
{
    const auto hdrColor = renderData.getTexture(kHdrColor);
    const auto color    = renderData.getTexture(kOutputColor);
    const auto extent   = hdrColor->getDesc().size;
    const auto levels   = pyramidLevels({extent.width, extent.height}, parameters.levels);
    using Stage         = Filter::Stage;
    const auto image    = [&](std::string_view stage, uint32_t level)
    { return renderData.getTexture(pyramidImageName(stage, level)); };

    std::array<rhi::ITexture*, kMaxBloomLevels> pyramid;
    auto*                                       source = hdrColor;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const auto horizontal = image(kHorizontal, level);
        const auto vertical   = image(kVertical, level);
        filters.emplace_back(Stage::DownsampleHorizontal, parameters).compile(*mpDevice, context, source, source, horizontal);
        filters.emplace_back(Stage::DownsampleVertical, parameters).compile(*mpDevice, context, horizontal, horizontal, vertical);
        pyramid[level] = vertical;
        source         = vertical;
    }

    auto blurred = pyramid[levels - 1];
    for (uint32_t level = levels - 1; level > 0; --level)
    {
        const auto base   = pyramid[level - 1];
        const auto target = image(kUpsample, level - 1);
        filters.emplace_back(Stage::Upsample, parameters).compile(*mpDevice, context, blurred, base, target);
        blurred = target;
    }
    filters.emplace_back(Stage::Composite, parameters).compile(*mpDevice, context, hdrColor, blurred, color);
}


void Bloom::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    auto&       commands = *renderCommands->commands;
    const auto& frame    = *renderCommands;
    const auto  size     = renderData.getTexture(kHdrColor)->getDesc().size;
    if (filters.empty())
        prepareFilters(frame, renderData);

    for (const auto& filter : filters)
        filter.execute(commands, {size.width, size.height}, mpScene->viewport());
}


void Bloom::Filter::compile(
    rhi::IDevice& device, const RenderContext& context,
    rhi::ITexture* input, rhi::ITexture* baseInput, rhi::ITexture* target
)
{
    const auto sourceExtent = input->getDesc().size;
    sourceTexelSize         = {1.0f / sourceExtent.width, 1.0f / sourceExtent.height};
    this->target = target;
    const auto shader       = context.shaderManager->getProgram(
        "passes/postprocess/bloom/bloom.slang",
        {"vertMain", "fragMain"},
        {{"BLOOM_STAGE", std::to_string(static_cast<int>(stage))}}
    );
    rhi::ColorTargetDesc color;
    color.format = target->getDesc().format;
    rhi::RenderPipelineDesc desc;
    desc.program                       = shader;
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(device.createRenderPipeline(desc, pipeline.writeRef()));
    rhiCheck(device.createRootShaderObject(desc.program, shaderObject.writeRef()));
    const rhi::ShaderCursor root(shaderObject);
    rhiCheck(root["sourceColor"].setBinding(input));
    rhiCheck(root["linearSampler"].setBinding(context.resources->sampler(BuiltinAssets::Sampler::linearClamp)));
    rhiCheck(root["baseColor"].setBinding(baseInput));
}


void Bloom::Filter::execute(rhi::ICommandEncoder& commands, Extent2D frameExtent, const ViewportRect& viewport) const
{
    const auto view = target->getDefaultView();
    CHECK(view);
    rhi::RenderPassColorAttachment color;
    color.view   = view;
    color.loadOp = rhi::LoadOp::DontCare;
    auto* pass   = commands.beginRenderPass({&color, 1});
    CHECK(pass);
    pass->bindPipeline(pipeline, shaderObject);
    const auto size = target->getDesc().size;
    pass->setRenderState(ViewportRect{0, 0, float(size.width), float(size.height)}.renderState());
    const BloomParameters parameters{
        .viewport =
            {viewport.x / frameExtent.width,
             viewport.y / frameExtent.height,
             (viewport.x + viewport.width) / frameExtent.width,
             (viewport.y + viewport.height) / frameExtent.height},
        .texelSize    = sourceTexelSize,
        .filterRadius = settings.filterRadius,
        .intensity    = settings.intensity,
        .scatter      = settings.scatter,
    };
    {
        const auto uniformValue  = parameters;
        const auto uniformCursor = rhi::ShaderCursor(shaderObject)["parameters"].getDereferenced();
        CHECK(uniformCursor.getTypeLayout()->getSize() <= sizeof(uniformValue));
        rhiCheck(uniformCursor.setData(&uniformValue, uniformCursor.getTypeLayout()->getSize()));
    };
    pass->draw({3});
    pass->end();
}
