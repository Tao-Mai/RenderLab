#include "passes/denoise/SVGF/SVGFPass.h"

#include "editor/PassEditor.h"
#include "render/RenderConfig.h"

#include <array>
#include <utility>

namespace
{
constexpr char kInputBufferAlbedo[] = "Albedo";
constexpr char kInputBufferColor[] = "Color";
constexpr char kInputBufferEmission[] = "Emission";
constexpr char kInputBufferWorldNormal[] = "WorldNormal";
constexpr char kInputBufferLinearZ[] = "LinearZ";
constexpr char kInternalBufferPreviousLinearZAndNormal[] = "Previous Linear Z and Packed Normal";
constexpr char kInternalBufferPreviousLighting[] = "Previous Lighting";
constexpr char kInternalBufferPreviousMoments[] = "Previous Moments";
constexpr char kOutputBufferFilteredImage[] = "Filtered image";

constexpr uint32_t kHistoryLayers = 2;
constexpr uint32_t kLightingHistoryLayers = kHistoryLayers * kLightingLayerCount;
}  // namespace

SVGFPass::SVGFPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<SvgfSettings>(properties))
{
    parameters.validate();
}

Falcor::Properties SVGFPass::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void SVGFPass::setProperties(const Falcor::Properties& properties)
{
    SvgfSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}

Falcor::RenderPassReflection SVGFPass::reflect(const CompileData& compileData)
{
    const auto* input = compileData.connectedResources.getField(kInputBufferColor);
    const auto extent = input ? Falcor::uint2{input->getWidth(), input->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    reflection.addInput(kInputBufferColor, "Direct and indirect radiance")
        .texture2D(0, 0, 1, 0, kLightingLayerCount)
        .bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    for (const auto* name : {kInputBufferAlbedo, kInputBufferEmission, kInputBufferWorldNormal, kInputBufferLinearZ})
        reflection.addInput(name, "Denoiser input texture")
            .texture2D(0, 0, 1, 0, 0)
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource);

    reflection.addInternal(kInternalBufferPreviousLinearZAndNormal, "Previous Linear Z and Packed Normal")
        .format(Falcor::ResourceFormat::RGBA32Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y, 1, 1, kHistoryLayers);
    reflection.addInternal(kInternalBufferPreviousLighting, "Previous Filtered Lighting")
        .format(Falcor::ResourceFormat::RGBA32Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y, 1, 1, kLightingHistoryLayers);
    reflection.addInternal(kInternalBufferPreviousMoments, "Previous Moments")
        .format(Falcor::ResourceFormat::RG32Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y, 1, 1, kLightingHistoryLayers);

    reflection.addOutput(kOutputBufferFilteredImage, "Denoised radiance")
        .format(Falcor::ResourceFormat::RGBA16Float)
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y);
    return reflection;
}

void SVGFPass::compile(Falcor::RenderContext*, const CompileData& compileData)
{
    const auto* input = compileData.connectedResources.getField(kInputBufferColor);
    DCHECK(input);
    const auto width = input->getWidth() ? input->getWidth() : compileData.defaultTexDims.x;
    const auto height = input->getHeight() ? input->getHeight() : compileData.defaultTexDims.y;

    constexpr auto kSignalUsage = Falcor::ResourceBindFlags::UnorderedAccess | Falcor::ResourceBindFlags::ShaderResource;
    for (auto* images : {&reprojected, &scratch})
        for (auto& image : *images)
            image = mpDevice->createTexture2D(width, height, Falcor::ResourceFormat::RGBA32Float, 1, 1, kSignalUsage);
    historyLength = mpDevice->createTexture2D(
        width, height, Falcor::ResourceFormat::R32Float, kHistoryLayers, 1, Falcor::ResourceBindFlags::UnorderedAccess
    );

    resourcesBound = false;
}

void SVGFPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache    = *renderCommands->vulkanResources;
    auto&       commands = *renderCommands->commands;
    const auto& frame    = *renderCommands;

    if (!resourcesBound)
    {
        const auto image = [&](std::string_view name) { return cache.registerImage(renderData.getTexture(name)); };
        const DenoiserInputs traced{
            .color       = image(kInputBufferColor),
            .depth       = image(kInputBufferLinearZ),
            .worldNormal = image(kInputBufferWorldNormal),
            .emission    = image(kInputBufferEmission),
            .albedo      = image(kInputBufferAlbedo),
        };
        const std::array history{
            image(kInternalBufferPreviousLinearZAndNormal),
            image(kInternalBufferPreviousLighting),
            image(kInternalBufferPreviousMoments),
            cache.registerImage(historyLength),
        };
        const std::array reprojectedImages{
            cache.registerImage(reprojected[kDirectLightingLayer]),
            cache.registerImage(reprojected[kIndirectLightingLayer]),
        };
        const auto lighting = reprojection.compile(frame, traced, history, reprojectedImages);

        const std::array scratchImages{
            cache.registerImage(scratch[kDirectLightingLayer]),
            cache.registerImage(scratch[kIndirectLightingLayer]),
        };
        filter.compile(frame, traced, lighting, scratchImages, image(kOutputBufferFilteredImage), reprojection);
        resourcesBound = true;
    }

    reprojection.execute(cache, commands, frame);
    filter.execute(cache, commands, frame);
}

void SVGFPass::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor(widget, *this, parameters, context->passEditor, "SVGF");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, SVGFPass>();
}
