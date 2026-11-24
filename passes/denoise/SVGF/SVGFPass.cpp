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
    const auto  extent = input ? glm::uvec2{input->texture().size.width, input->texture().size.height} : glm::uvec2{};

    RenderPassReflection reflection;
    reflection.addInput(kInputBufferColor, rhi::TextureDesc{
        .type = rhi::TextureType::Texture2DArray,
        .size = {0, 0, 1},
        .arrayLength = kLightingLayerCount,
        .mipCount = 0,
        .usage = rhi::TextureUsage::ShaderResource,
    });
    for (const auto* name : {kInputBufferAlbedo, kInputBufferEmission, kInputBufferWorldNormal, kInputBufferLinearZ})
        reflection.addInput(name, rhi::TextureDesc{
            .size = {0, 0, 1},
            .arrayLength = 0,
            .mipCount = 0,
            .usage = rhi::TextureUsage::ShaderResource,
        });

    reflection.addInternal(kInternalBufferPreviousLinearZAndNormal, rhi::TextureDesc{
        .type = rhi::TextureType::Texture2DArray,
        .size = {extent.x, extent.y, 1},
        .arrayLength = kHistoryLayers,
        .format = rhi::Format::RGBA32Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    reflection.addInternal(kInternalBufferPreviousLighting, rhi::TextureDesc{
        .type = rhi::TextureType::Texture2DArray,
        .size = {extent.x, extent.y, 1},
        .arrayLength = kLightingHistoryLayers,
        .format = rhi::Format::RGBA32Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    reflection.addInternal(kInternalBufferPreviousMoments, rhi::TextureDesc{
        .type = rhi::TextureType::Texture2DArray,
        .size = {extent.x, extent.y, 1},
        .arrayLength = kLightingHistoryLayers,
        .format = rhi::Format::RG32Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });

    reflection.addOutput(kOutputBufferFilteredImage, rhi::TextureDesc{
        .size = {extent.x, extent.y, 1},
        .format = rhi::Format::RGBA16Float,
        .usage = rhi::TextureUsage::UnorderedAccess,
    });
    return reflection;
}

void SVGFPass::compile(RenderContext*, const CompileData& compileData)
{
    const auto* input = compileData.connectedResources.getField(kInputBufferColor);

    const auto width  = input->texture().size.width ? input->texture().size.width : compileData.defaultTexDims.x;
    const auto height = input->texture().size.height ? input->texture().size.height : compileData.defaultTexDims.y;


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
    const auto& frame = *renderCommands;

    if (!resourcesBound)
    {
        const SVGFInputs traced{
            .color       = renderData.getTexture(kInputBufferColor),
            .depth       = renderData.getTexture(kInputBufferLinearZ),
            .worldNormal = renderData.getTexture(kInputBufferWorldNormal),
            .emission    = renderData.getTexture(kInputBufferEmission),
            .albedo      = renderData.getTexture(kInputBufferAlbedo),
        };
        const std::array history{
            renderData.getTexture(kInternalBufferPreviousLinearZAndNormal),
            renderData.getTexture(kInternalBufferPreviousLighting),
            renderData.getTexture(kInternalBufferPreviousMoments),
            historyLength.get(),
        };
        const std::array reprojectedImages{
            reprojected[kDirectLightingLayer].get(),
            reprojected[kIndirectLightingLayer].get(),
        };
        const auto lighting = reprojection.compile(frame, traced, history, reprojectedImages);

        const std::array scratchImages{
            scratch[kDirectLightingLayer].get(),
            scratch[kIndirectLightingLayer].get(),
        };
        filter.compile(
            frame,
            traced,
            lighting,
            scratchImages,
            renderData.getTexture(kOutputBufferFilteredImage),
            reprojection
        );
        resourcesBound = true;
    }

    reprojection.execute(frame, renderData.getHistoryRevision(kInputBufferColor));
    filter.execute(frame);
}

void SVGFPass::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    drawPassEditor(widget, *this, parameters, context, "SVGF");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SVGFPass>();
}
