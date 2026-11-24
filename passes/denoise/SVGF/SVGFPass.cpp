#include "passes/denoise/SVGF/SVGFPass.h"

#include "editor/PassEditor.h"
#include "render/RenderConfig.h"

#include <array>
#include <utility>

namespace
{
constexpr char kInputBufferAlbedo[]                      = "Albedo";
constexpr char kInputBufferColor[]                       = "Color";
constexpr char kInputBufferEmission[]                    = "Emission";
constexpr char kInputBufferWorldNormal[]                 = "WorldNormal";
constexpr char kInputBufferLinearZ[]                     = "LinearZ";
constexpr char kInternalBufferPreviousLinearZAndNormal[] = "Previous Linear Z and Packed Normal";
constexpr char kInternalBufferPreviousLighting[]         = "Previous Lighting";
constexpr char kInternalBufferPreviousMoments[]          = "Previous Moments";
constexpr char kOutputBufferFilteredImage[]              = "Filtered image";

constexpr uint32_t kHistoryLayers         = 2;
constexpr uint32_t kLightingHistoryLayers = kHistoryLayers * kLightingLayerCount;
}  // namespace

SVGFPass::SVGFPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device)), parameters(deserializeFromProperties<SvgfSettings>(properties))
{
    parameters.validate();
}

Properties SVGFPass::getProperties() const { return serializeToProperties(parameters); }

void SVGFPass::setProperties(const Properties& properties)
{
    SvgfSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}

RenderPassReflection SVGFPass::reflect(const CompileData& compileData)
{
    const auto* input  = compileData.connectedResources.getField(kInputBufferColor);
    const auto  extent = input ? glm::uvec2{input->getWidth(), input->getHeight()} : glm::uvec2{};

    RenderPassReflection reflection;
    reflection.addInput(kInputBufferColor, "Direct and indirect radiance")
        .texture2D(0, 0, 1, 0, kLightingLayerCount)
        .bindFlags(ResourceBindFlags::ShaderResource);
    for (const auto* name : {kInputBufferAlbedo, kInputBufferEmission, kInputBufferWorldNormal, kInputBufferLinearZ})
        reflection.addInput(name, "Denoiser input texture")
            .texture2D(0, 0, 1, 0, 0)
            .bindFlags(ResourceBindFlags::ShaderResource);

    reflection.addInternal(kInternalBufferPreviousLinearZAndNormal, "Previous Linear Z and Packed Normal")
        .format(rhi::Format::RGBA32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y, 1, 1, kHistoryLayers);
    reflection.addInternal(kInternalBufferPreviousLighting, "Previous Filtered Lighting")
        .format(rhi::Format::RGBA32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y, 1, 1, kLightingHistoryLayers);
    reflection.addInternal(kInternalBufferPreviousMoments, "Previous Moments")
        .format(rhi::Format::RG32Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y, 1, 1, kLightingHistoryLayers);

    reflection.addOutput(kOutputBufferFilteredImage, "Denoised radiance")
        .format(rhi::Format::RGBA16Float)
        .bindFlags(ResourceBindFlags::UnorderedAccess)
        .texture2D(extent.x, extent.y);
    return reflection;
}

void SVGFPass::compile(RenderContext*, const CompileData& compileData)
{
    const auto* input = compileData.connectedResources.getField(kInputBufferColor);

    const auto width  = input->getWidth() ? input->getWidth() : compileData.defaultTexDims.x;
    const auto height = input->getHeight() ? input->getHeight() : compileData.defaultTexDims.y;


    rhi::TextureDesc desc;
    desc.size   = {width, height, 1};
    desc.format = rhi::Format::RGBA32Float;
    desc.usage  = rhi::TextureUsage::UnorderedAccess | rhi::TextureUsage::ShaderResource;
    for (auto* images : {&reprojected, &scratch})
        for (auto& image : *images)
            rhiCheck(mpDevice->createTexture(desc, nullptr, image.writeRef()));
    desc.type        = rhi::TextureType::Texture2DArray;
    desc.arrayLength = kHistoryLayers;
    desc.format      = rhi::Format::R32Float;
    desc.usage       = rhi::TextureUsage::UnorderedAccess;
    rhiCheck(mpDevice->createTexture(desc, nullptr, historyLength.writeRef()));
    resourcesBound = false;
}

void SVGFPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    auto&       cache    = *renderCommands->gpuResources;
    auto&       commands = *renderCommands->commands;
    const auto& frame    = *renderCommands;

    if (!resourcesBound)
    {
        const auto image = [&](std::string_view name) { return cache.registerImage(renderData.getTexture(name)); };
        const SVGFInputs traced{
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

void SVGFPass::renderUI(RenderContext* context, Gui::Widgets& widget)
{
    drawPassEditor(widget, *this, parameters, context->passEditor, "SVGF");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SVGFPass>();
}
