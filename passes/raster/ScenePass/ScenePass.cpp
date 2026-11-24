#include "passes/raster/ScenePass/ScenePass.h"

#include "core/Logger.h"
#include "core/Serializer.h"

#include <slang-rhi.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/plugins/Plugin.h"
#include "render/raster/MeshDraw.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/MaterialTable.h"

ScenePass::ScenePass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties) :
    RenderPass(std::move(device)), parameters(DeserializeWithDefaults<RasterLightingOutputSettings>(properties))
{
    parameters.validate();
}

void ScenePass::onSettingsUpdated(const RasterLightingOutputSettings& previous)
{
    if (parameters.hdr != previous.hdr)
        requestRecompile();
}

RenderPassReflection ScenePass::reflect(const ResourceCache::DefaultProperties& defaults)
{
    RenderPassReflection reflection;
    addShadowInputs(reflection);
    reflection.addInput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::D32Float,
        .usage = rhi::TextureUsage::DepthStencil,
    });
    reflection.addField({
        .name = "color",
        .resource = rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = parameters.getColorFormat(defaults.format),
            .usage = rhi::TextureUsage::RenderTarget,
        },
        .visibility = RenderPassReflection::Visibility::Output,
        .matchInput = "depth",
        .textureMatch = RenderPassReflection::TextureMatch::Extent,
    });
    return reflection;
}


void ScenePass::compile(RenderContext*, const CompileData& compileData)
{
    parameters.validateColorFormat(*mpDevice);
    const rhi::DepthStencilDesc depth{
        .format           = rhi::Format::D32Float,
        .depthTestEnable  = true,
        .depthWriteEnable = false,
        .depthFunc        = rhi::ComparisonFunc::Equal
    };
    pipelines.setup(
        *mpDevice,
        compileData.getTextureDesc("color").format,
        depth,
        false
    );
}


void ScenePass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto target    = renderData.getTexture("color");
    const auto colorView = target->getDefaultView();
    CHECK(colorView);
    const auto depthView = renderData.getTexture("depth")->getDefaultView();
    CHECK(depthView);
    rhi::RenderPassColorAttachment color;
    color.view          = colorView;
    color.loadOp        = rhi::LoadOp::Clear;
    color.clearValue[3] = 1.0f;
    rhi::RenderPassDepthStencilAttachment depth;
    depth.view          = depthView;
    depth.depthLoadOp   = rhi::LoadOp::Load;
    depth.depthReadOnly = true;
    auto* pass          = renderCommands->commands->beginRenderPass({&color, 1, &depth});
    CHECK(pass);
    const auto&      size     = target->getDesc().size;
    const auto       viewport = mpScene->viewport().clamped({size.width, size.height});
    const ShadowBindings shadows{renderData, *renderCommands->resources};
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *mpScene};
    recorder.drawOpaque(
        [&](const auto& material) -> rhi::IShaderObject&
        {
            return pipelines.bind(*pass, *renderCommands->shaderManager, *mpScene, material, shadows,
                parameters.ambientCoefficient, true);
        }
    );
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ScenePass>();
}
