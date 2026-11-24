#include "passes/raster/DeferredLightingPass/DeferredLightingPass.h"

#include "core/Logger.h"
#include "core/Plugin.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <utility>

namespace
{
constexpr std::string_view kDeferredLightingShader = "deferred_lighting";
constexpr std::array       kGBufferChannels{
    std::pair{"diffuseOcclusion", "diffuseOcclusion"},
    std::pair{"normalRoughness", "normalRoughness"},
    std::pair{"emissiveSpecularF90", "emissiveSpecularF90"},
    std::pair{"specularF0DiffuseFresnel", "specularF0DiffuseFresnel"},
    std::pair{"sheenColorRoughness", "sheenColorRoughness"},
    std::pair{"depth", "sceneDepth"}
};
}  // namespace


DeferredLightingPass::DeferredLightingPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device)),
    parameters(deserializeFromProperties<RasterLightingOutputSettings>(properties))
{
    parameters.validate();
}


Properties DeferredLightingPass::getProperties() const { return serializeToProperties(parameters); }


void DeferredLightingPass::setProperties(const Properties& properties)
{
    RasterLightingOutputSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.hdr != parameters.hdr;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}


RenderPassReflection DeferredLightingPass::reflect(const CompileData& compileData)
{
    const auto* depth  = compileData.connectedResources.getField("depth");
    const auto  extent = depth ? glm::uvec2{depth->getWidth(), depth->getHeight()} : glm::uvec2{};

    RenderPassReflection reflection;
    addShadowInputs(reflection);
    for (const auto& channel : kGBufferChannels)
        reflection.addInput(channel.first, "G-buffer texture")
            .texture2D(0, 0, 1, 0, 0)
            .bindFlags(ResourceBindFlags::ShaderResource);
    reflection.addOutput("color", "Raster lighting result")
        .format(parameters.getColorFormat(compileData.defaultTexFormat))
        .bindFlags(ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}


void DeferredLightingPass::compile(RenderContext* passContext, const CompileData&)
{
    parameters.validateColorFormat(*passContext->device);
    pipelines = {};
}

rhi::IRenderPipeline* DeferredLightingPass::getPipeline(const RenderContext& context, rhi::Format format)
{
    const bool hasEnvironment = context.scene->environmentMap != nullptr;
    auto& pipeline = pipelines[hasEnvironment];
    if (pipeline)
        return pipeline;

    const ShaderDefines variant{
        {"HAS_ENV", hasEnvironment ? "1" : "0"},
        {"ENABLE_CLEARCOAT", "0"},
        {"ENABLE_SHEEN", "1"}
    };
    rhi::ColorTargetDesc color;
    color.format = format;
    rhi::RenderPipelineDesc desc;
    desc.program =
        context.shaderManager->program(context.shaderManager->getOrCompile(kDeferredLightingShader, variant));
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(context.device->createRenderPipeline(desc, pipeline.writeRef()));
    return pipeline;
}


void DeferredLightingPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto target    = renderData.getTexture("color");
    auto* pipeline       = getPipeline(*renderCommands, target->getDesc().format);
    const auto colorView = target->getDefaultView();
    CHECK(colorView);
    rhi::RenderPassColorAttachment color;
    color.view   = colorView;
    color.loadOp = rhi::LoadOp::DontCare;
    auto* pass   = renderCommands->commands->beginRenderPass({&color, 1});
    CHECK(pass);
    auto* object = pass->bindPipeline(pipeline);
    CHECK(object);
    const rhi::ShaderCursor root(object);
    const auto&             size     = target->getDesc().size;
    const auto              viewport = renderCommands->view->viewport.clamped({size.width, size.height});
    const auto& scene = *renderCommands->scene;
    rhiCheck(root["sceneGlobals"].setBinding(scene.globalsBuffer()));
    scene.lighting.bindRaster(root, parameters.ambientCoefficient, scene.environmentMap);
    const ShadowBindings shadows{renderData, *renderCommands->resources};
    shadows.bind(root);
    for (const auto& [name, parameter] : kGBufferChannels)
        rhiCheck(root[parameter].setBinding(renderData.getTexture(name)->getDefaultView()));
    const DeferredLightingPushConstants constants{{viewport.x, viewport.y, viewport.width, viewport.height}};
    rhiCheck(root["parameters"].getDereferenced().setData(constants));
    pass->setRenderState(viewport.renderState());
    pass->draw({3});
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, DeferredLightingPass>();
}
