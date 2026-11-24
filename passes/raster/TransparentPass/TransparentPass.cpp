#include "passes/raster/TransparentPass/TransparentPass.h"

#include "core/Plugin.h"

#include "render/GpuScene.h"
#include "render/RenderContext.h"
#include "render/raster/MeshDraw.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/MaterialTable.h"

#include <algorithm>
#include <glm/geometric.hpp>

TransparentPass::TransparentPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device)), parameters(deserializeFromProperties<TransparentLightingSettings>(properties))
{
    parameters.validate();
}

Properties TransparentPass::getProperties() const { return serializeToProperties(parameters); }

void TransparentPass::setProperties(const Properties& properties)
{
    TransparentLightingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}

RenderPassReflection TransparentPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    addShadowInputs(reflection);
    const auto* input = compileData.connectedResources.getField("color");
    reflection.addInputOutput("color", rhi::TextureDesc{
        .size = input ? input->texture().size : rhi::Extent3D{0, 0, 1},
        .format = input ? input->texture().format : rhi::Format::Undefined,
        .usage = rhi::TextureUsage::RenderTarget,
    });
    reflection.addInput("depth", rhi::TextureDesc{
        .size = {0, 0, 1},
        .format = rhi::Format::D32Float,
        .usage = rhi::TextureUsage::DepthStencil,
    });
    return reflection;
}


void TransparentPass::compile(RenderContext*, const CompileData& compileData)
{
    const rhi::DepthStencilDesc depth{
        .format           = rhi::Format::D32Float,
        .depthTestEnable  = true,
        .depthWriteEnable = false,
        .depthFunc        = rhi::ComparisonFunc::Less
    };
    pipelines.setup(
        *mpDevice,
        compileData.connectedResources.getField("color")->texture().format,
        depth,
        true
    );
}


void TransparentPass::sortDrawItems()
{
    sortedDrawItems.clear();
    const auto& materials = mpScene->materials();
    const auto draws = mpScene->drawCalls();
    for (const uint32_t itemIndex : mpScene->visibleDraws())
    {
        const auto& item = draws[itemIndex];
        const auto materialIndex = item.materialIndex;
        if (materials.info(materialIndex).alphaMode == kAlphaModeBlend)
            sortedDrawItems.push_back(itemIndex);
    }
    const glm::vec3 cameraPosition{mpScene->cameraUniforms().cameraPosition};
    std::ranges::stable_sort(
        sortedDrawItems,
        std::greater{},
        [&](uint32_t itemIndex)
        {
            const auto offset = glm::vec3{draws[itemIndex].model[3]} - cameraPosition;
            return glm::dot(offset, offset);
        }
    );
}


void TransparentPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    sortDrawItems();
    if (sortedDrawItems.empty())
        return;

    const auto target    = renderData.getTexture("color");
    const auto colorView = target->getDefaultView();
    CHECK(colorView);
    const auto depthView = renderData.getTexture("depth")->getDefaultView();
    CHECK(depthView);
    rhi::RenderPassColorAttachment color;
    color.view          = colorView;
    color.loadOp        = rhi::LoadOp::Load;
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
    recorder.drawOrdered(
        sortedDrawItems,
        [&](const auto& material) -> rhi::IShaderObject&
        {
            return pipelines.bind(*pass, *renderCommands->shaderManager, *mpScene, material, shadows,
                parameters.ambientCoefficient, parameters.clearcoat);
        }
    );
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, TransparentPass>();
}
