#include "passes/raster/ScenePass/ScenePass.h"

#include "core/Logger.h"
#include "core/Plugin.h"

#include <slang-rhi.h>
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/raster/MeshDraw.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/MaterialTable.h"

ScenePass::ScenePass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device)), parameters(deserializeFromProperties<RasterLightingOutputSettings>(properties))
{
    parameters.validate();
}


Properties ScenePass::getProperties() const { return serializeToProperties(parameters); }


void ScenePass::setProperties(const Properties& properties)
{
    RasterLightingOutputSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.hdr != parameters.hdr;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}


RenderPassReflection ScenePass::reflect(const CompileData& compileData)
{
    const auto* depth  = compileData.connectedResources.getField("depth");
    const auto  extent = depth ? glm::uvec2{depth->texture().size.width, depth->texture().size.height} : glm::uvec2{};

    RenderPassReflection reflection;
    addShadowInputs(reflection);
    reflection.addInput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::D32Float,
        .usage = rhi::TextureUsage::DepthStencil,
    });
    reflection.addOutput("color", rhi::TextureDesc{
        .size = {extent.x, extent.y, 1},
        .format = parameters.getColorFormat(compileData.defaultTexFormat),
        .usage = rhi::TextureUsage::RenderTarget,
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
        parameters.getColorFormat(compileData.defaultTexFormat),
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
