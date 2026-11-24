#include "passes/postprocess/Bloom/Bloom.h"

#include "core/Logger.h"
#include "core/profiling/Profiler.h"

#include <slang-rhi.h>
#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
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

struct alignas(16) BloomParameters
{
    glm::vec4 viewport;  // Normalized min/max, identical across the image pyramid.
    glm::vec2 texelSize;
    float     filterRadius;
    float     intensity;
    float     scatter;
};
static_assert(sizeof(BloomParameters) == 48);

uint32_t pyramidLevels(Falcor::uint2 extent, uint32_t limit)
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


Bloom::Bloom(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<BloomSettings>(properties))
{
    parameters.validate();
}


Falcor::Properties Bloom::getProperties() const { return Falcor::serializeToProperties(parameters); }


void Bloom::setProperties(const Falcor::Properties& properties)
{
    BloomSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.levels != parameters.levels;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}


Falcor::RenderPassReflection Bloom::reflect(const CompileData& compileData)
{
    auto extent = compileData.defaultTexDims;
    if (const auto* input = compileData.connectedResources.getField("hdrColor"))
    {
        extent.x = input->getWidth() ? input->getWidth() : extent.x;
        extent.y = input->getHeight() ? input->getHeight() : extent.y;
    }

    Falcor::RenderPassReflection reflection;
    reflection.addInput("hdrColor", "HDR scene color")
        .bindFlags(Falcor::ResourceBindFlags::ShaderResource)
        .format(rhi::Format::RGBA16Float);
    reflection.addOutput("color", "Scene color with bloom")
        .format(rhi::Format::RGBA16Float)
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);

    const auto levels   = pyramidLevels(extent, parameters.levels);
    auto       size     = extent;
    const auto addImage = [&](std::string_view stage, uint32_t level)
    {
        reflection.addInternal(pyramidImageName(stage, level), "Bloom pyramid image")
            .texture2D(size.x, size.y)
            .format(rhi::Format::RGBA16Float)
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource | Falcor::ResourceBindFlags::RenderTarget);
    };
    for (uint32_t level = 0; level < levels; ++level)
    {
        size = {std::max(1u, size.x / 2), std::max(1u, size.y / 2)};
        addImage(kHorizontal, level);
        addImage(kVertical, level);
        if (level + 1 < levels)
            addImage(kUpsample, level);
    }
    return reflection;
}


void Bloom::compile(Falcor::RenderContext* passContext, const CompileData&) { filters.clear(); }


void Bloom::prepareFilters(const Falcor::RenderContext& context, const Falcor::RenderData& renderData)
{
    auto&      cache    = *context.gpuResources;
    const auto hdrColor = cache.registerImage(renderData.getTexture("hdrColor"));
    const auto color    = cache.registerImage(renderData.getTexture("color"));
    const auto extent   = cache.imageInfo(hdrColor).size;
    const auto levels   = pyramidLevels({extent.width, extent.height}, parameters.levels);
    using Stage         = Filter::Stage;
    const auto image    = [&](std::string_view stage, uint32_t level)
    { return cache.registerImage(renderData.getTexture(pyramidImageName(stage, level))); };

    std::array<GpuResources::ImageHandle, kMaxBloomLevels> pyramid;
    auto                                                   source = hdrColor;
    for (uint32_t level = 0; level < levels; ++level)
    {
        const auto horizontal = image(kHorizontal, level);
        const auto vertical   = image(kVertical, level);
        filters.emplace_back(Stage::DownsampleHorizontal, parameters).compile(context, source, source, horizontal);
        filters.emplace_back(Stage::DownsampleVertical, parameters).compile(context, horizontal, horizontal, vertical);
        pyramid[level] = vertical;
        source         = vertical;
    }

    auto blurred = pyramid[levels - 1];
    for (uint32_t level = levels - 1; level > 0; --level)
    {
        const auto base   = pyramid[level - 1];
        const auto target = image(kUpsample, level - 1);
        filters.emplace_back(Stage::Upsample, parameters).compile(context, blurred, base, target);
        blurred = target;
    }
    filters.emplace_back(Stage::Composite, parameters).compile(context, hdrColor, blurred, color);
}


void Bloom::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache    = *renderCommands->gpuResources;
    auto&       commands = *renderCommands->commands;
    const auto& frame    = *renderCommands;
    const auto  extent   = renderData.getDefaultTextureDims();
    if (filters.empty())
        prepareFilters(frame, renderData);

    for (const auto& filter : filters)
        filter.execute(cache, commands, {extent.x, extent.y}, frame.view->viewport);
}


void Bloom::Filter::compile(
    const Falcor::RenderContext& context,
    GpuResources::ImageHandle    input,
    GpuResources::ImageHandle    baseInput,
    GpuResources::ImageHandle    target
)
{
    auto& cache  = *context.gpuResources;
    source       = input;
    this->target = target;
    const auto shader =
        context.shaderManager->getOrLoad("bloom", {{"BLOOM_STAGE", std::to_string(static_cast<int>(stage))}});
    rhi::ColorTargetDesc color;
    color.format = kHdrColorFormat;
    rhi::RenderPipelineDesc desc;
    desc.program                       = context.shaderManager->program(shader);
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(context.device->createRenderPipeline(desc, pipeline.writeRef()));
    rhiCheck(context.device->createRootShaderObject(desc.program, shaderObject.writeRef()));
    const rhi::ShaderCursor root(shaderObject);
    rhiCheck(root["sourceColor"].setBinding(cache.getImage(source)->getDefaultView()));
    rhiCheck(root["linearSampler"].setBinding(context.resources->sampler(BuiltinAssets::Sampler::linearClamp)));
    rhiCheck(root["baseColor"].setBinding(cache.getImage(baseInput)->getDefaultView()));
}


void Bloom::Filter::execute(
    GpuResources& cache, rhi::ICommandEncoder& commands, Extent2D frameExtent, const ViewportRect& viewport
) const
{

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


    const auto            sourceExtent = cache.imageInfo(source).size;
    const BloomParameters parameters{
        .viewport =
            {viewport.x / frameExtent.width,
             viewport.y / frameExtent.height,
             (viewport.x + viewport.width) / frameExtent.width,
             (viewport.y + viewport.height) / frameExtent.height},
        .texelSize    = {1.0f / sourceExtent.width, 1.0f / sourceExtent.height},
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
    recordDraw(context().profiler, 3);
    pass->end();
}
