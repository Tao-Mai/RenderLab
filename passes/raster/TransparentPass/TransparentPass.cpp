#include "passes/raster/TransparentPass/TransparentPass.h"

#include "core/Plugin.h"

#include "render/PreparedScene.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/raster/MeshDraw.h"
#include "render/raster/ShadowResources.h"
#include "render/resources/MaterialTable.h"

#include <algorithm>
#include <glm/geometric.hpp>

TransparentPass::TransparentPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
    RenderPass(std::move(device), SceneInputs::RasterVisibility),
    parameters(deserializeFromProperties<TransparentLightingSettings>(properties))
{
    parameters.validate();
}

Properties TransparentPass::getProperties() const { return serializeToProperties(parameters); }

void TransparentPass::setProperties(const Properties& properties)
{
    TransparentLightingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    const bool recompile = next.clearcoat != parameters.clearcoat;
    parameters           = std::move(next);
    if (recompile)
        requestRecompile();
}

RenderPassReflection TransparentPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflection;
    addShadowInputs(reflection);
    auto& color = reflection.addInputOutput("color", "Color target").bindFlags(ResourceBindFlags::RenderTarget);
    if (const auto* input = compileData.connectedResources.getField("color"))
        color.texture2D(input->getWidth(), input->getHeight()).format(input->getFormat());
    reflection.addInput("depth", "Scene depth")
        .format(rhi::Format::D32Float)
        .bindFlags(ResourceBindFlags::DepthStencil);
    return reflection;
}


void TransparentPass::compile(RenderContext* passContext, const CompileData& compileData)
{
    const rhi::DepthStencilDesc depth{
        .format           = rhi::Format::D32Float,
        .depthTestEnable  = true,
        .depthWriteEnable = false,
        .depthFunc        = rhi::ComparisonFunc::Less
    };
    pipelines.setup(
        *passContext->device,
        compileData.connectedResources.getField("color")->getFormat(),
        depth,
        true,
        parameters.clearcoat
    );
}


void TransparentPass::sortDrawItems(const RenderContext& frame)
{
    sortedDrawItems.clear();
    const auto& materials = *frame.materialTable;
    for (uint32_t itemIndex = 0; itemIndex < frame.scene->drawItems.size(); ++itemIndex)
    {
        if (!frame.scene->isVisible(itemIndex))
            continue;
        const auto materialIndex = frame.scene->materialIndex(itemIndex);
        if (materials.info(materialIndex).alphaMode == kAlphaModeBlend)
            sortedDrawItems.push_back(itemIndex);
    }
    std::ranges::stable_sort(
        sortedDrawItems,
        std::greater{},
        [&](uint32_t itemIndex)
        {
            const auto offset =
                glm::vec3{frame.scene->drawItems[itemIndex].model[3]} - glm::vec3{frame.view->uniforms.cameraPosition};
            return glm::dot(offset, offset);
        }
    );
}


void TransparentPass::execute(RenderContext* renderCommands, const RenderData& renderData)
{
    sortDrawItems(*renderCommands);
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
    const auto       viewport = renderCommands->view->viewport.clamped({size.width, size.height});
    const ShadowBindings shadows{renderData, *renderCommands->resources};
    MeshDrawRecorder recorder{*pass, viewport.renderState(), *renderCommands, &shadows, parameters.ambientCoefficient};
    recorder.drawOrdered(
        sortedDrawItems,
        [&](const auto& material) { return pipelines.get(*renderCommands, material); }
    );
    pass->end();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, TransparentPass>();
}
