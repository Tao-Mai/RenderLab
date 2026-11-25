#include "shaders/passes/denoise/SVGF/SVGFPass.h"

#include "core/Serializer.h"
#include "render/GpuScene.h"
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
constexpr rhi::TextureDesc kAuxiliaryInput{
    .size = {0, 0, 1},
    .arrayLength = 0,
    .mipCount = 0,
    .usage = rhi::TextureUsage::ShaderResource,
};
}  // namespace

SVGFPass::SVGFPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
    RenderPass(std::move(device)), parameters(DeserializeWithDefaults<SvgfSettings>(properties))
{
    parameters.validate();
}

const RenderPassReflection::Field SVGFPass::kPorts[]{
    {
        .name = kInputBufferColor,
        .resource = rhi::TextureDesc{
            .type = rhi::TextureType::Texture2DArray,
            .size = {0, 0, 1},
            .arrayLength = kLightingLayerCount,
            .mipCount = 0,
            .usage = rhi::TextureUsage::ShaderResource,
        },
        .visibility = RenderPassReflection::Visibility::Input,
    },
    {kInputBufferAlbedo, kAuxiliaryInput, RenderPassReflection::Visibility::Input},
    {kInputBufferEmission, kAuxiliaryInput, RenderPassReflection::Visibility::Input},
    {kInputBufferWorldNormal, kAuxiliaryInput, RenderPassReflection::Visibility::Input},
    {kInputBufferLinearZ, kAuxiliaryInput, RenderPassReflection::Visibility::Input},
    {
        .name = kInternalBufferPreviousLinearZAndNormal,
        .resource = rhi::TextureDesc{
            .type = rhi::TextureType::Texture2DArray,
            .size = {0, 0, 1},
            .arrayLength = kHistoryLayers,
            .format = rhi::Format::RGBA32Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Internal,
        .matchInput = kInputBufferColor,
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
    {
        .name = kInternalBufferPreviousLighting,
        .resource = rhi::TextureDesc{
            .type = rhi::TextureType::Texture2DArray,
            .size = {0, 0, 1},
            .arrayLength = kLightingHistoryLayers,
            .format = rhi::Format::RGBA32Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Internal,
        .matchInput = kInputBufferColor,
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
    {
        .name = kInternalBufferPreviousMoments,
        .resource = rhi::TextureDesc{
            .type = rhi::TextureType::Texture2DArray,
            .size = {0, 0, 1},
            .arrayLength = kLightingHistoryLayers,
            .format = rhi::Format::RG32Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Internal,
        .matchInput = kInputBufferColor,
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
    {
        .name = kOutputBufferFilteredImage,
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::RGBA16Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        },
        .visibility = RenderPassReflection::Visibility::Output,
        .matchInput = kInputBufferColor,
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    },
};

void SVGFPass::compile(RenderContext*, const CompileData& compileData)
{
    const auto size = compileData.getTextureDesc(kInputBufferColor).size;

    rhi::TextureDesc desc;
    desc.size   = {size.width, size.height, 1};
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
        auto& shaders = *frame.shaderManager;
        const auto lighting = reprojection.compile(*mpDevice, shaders, traced, history, reprojectedImages);

        const std::array scratchImages{
            scratch[kDirectLightingLayer].get(),
            scratch[kIndirectLightingLayer].get(),
        };
        filter.compile(
            *mpDevice,
            shaders,
            traced,
            lighting,
            scratchImages,
            renderData.getTexture(kOutputBufferFilteredImage),
            reprojection
        );
        resourcesBound = true;
    }

    reprojection.execute(*frame.commands, *mpScene, renderData.getHistoryRevision(kInputBufferColor));
    filter.execute(*frame.commands);
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, SVGFPass>();
}
