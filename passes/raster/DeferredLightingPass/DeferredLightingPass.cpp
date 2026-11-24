#include "passes/raster/DeferredLightingPass/DeferredLightingPass.h"

#include "core/Logger.h"
#include "core/Plugin.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#include "passes/raster/RasterPbr.h"
#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <utility>

namespace
{
struct DeferredLightingPushConstants
{
    glm::vec4 viewport{0.0f};
};
static_assert(sizeof(DeferredLightingPushConstants) == 16);

constexpr bool             kEnableSheen            = true;
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
    const auto  extent = depth ? glm::uvec2{depth->texture().size.width, depth->texture().size.height} : glm::uvec2{};

    RenderPassReflection reflection;
    addShadowInputs(reflection);
    for (const auto& channel : kGBufferChannels)
        reflection.addInput(channel.first, rhi::TextureDesc{
            .size = {0, 0, 1},
            .arrayLength = 0,
            .mipCount = 0,
            .usage = rhi::TextureUsage::ShaderResource,
        });
    reflection.addOutput("color", rhi::TextureDesc{
        .size = {extent.x, extent.y, 1},
        .format = parameters.getColorFormat(compileData.defaultTexFormat),
        .usage = rhi::TextureUsage::RenderTarget,
    });
    return reflection;
}


void DeferredLightingPass::compile(RenderContext*, const CompileData&)
{
    parameters.validateColorFormat(*mpDevice);
    pipelines = {};
}

rhi::IRenderPipeline*
DeferredLightingPass::getPipeline(const RenderContext& context, rhi::Format format, bool hasEnvironment)
{
    auto& pipeline = pipelines[hasEnvironment];
    if (pipeline)
        return pipeline;

    const ShaderDefines variant{
        {"HAS_ENV", hasEnvironment ? "1" : "0"},
        {"ENABLE_CLEARCOAT", "0"},
        {"ENABLE_SHEEN", kEnableSheen ? "1" : "0"}
    };
    rhi::ColorTargetDesc color;
    color.format = format;
    rhi::RenderPipelineDesc desc;
    desc.program = context.shaderManager->getProgram(
        "passes/raster/DeferredLightingPass/deferred_lighting.slang",
        {"vertMain", "fragMain"},
        variant
    );
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(mpDevice->createRenderPipeline(desc, pipeline.writeRef()));
    return pipeline;
}


void DeferredLightingPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    const auto& scene          = *mpScene;
    const bool  hasEnvironment = scene.environmentMap() != nullptr;
    const auto  target         = renderData.getTexture("color");
    auto*       pipeline       = getPipeline(*renderCommands, target->getDesc().format, hasEnvironment);
    const auto  colorView      = target->getDefaultView();
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
    const auto              viewport = scene.viewport().clamped({size.width, size.height});
    scene.bindShaderData(*object);
    if (!hasEnvironment)
        rhiCheck(
            root["lightingParameters"].getDereferenced()["ambientCoefficient"].setData(parameters.ambientCoefficient)
        );
    bindPbrTextures(root, scene, kEnableSheen);
    const ShadowBindings shadows{renderData, *renderCommands->resources};
    shadows.bind(root);
    for (const auto& [name, parameter] : kGBufferChannels)
        rhiCheck(root[parameter].setBinding(renderData.getTexture(name)));
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
