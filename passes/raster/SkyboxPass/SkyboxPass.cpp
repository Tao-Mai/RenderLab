#include "render/device/ResourceBarriers.h"
#include "passes/raster/SkyboxPass/SkyboxPass.h"

#include "core/profiling/Profiler.h"
#include "core/Plugin.h"

#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/resources/NativeFormats.h"
#include "render/resources/ShaderData.h"

#include <array>

Falcor::RenderPassReflection SkyboxPass::reflect(const CompileData& compileData)
{
    Falcor::RenderPassReflection reflection;
    auto& color = reflection.addInputOutput("color", "Color target").bindFlags(Falcor::ResourceBindFlags::RenderTarget);
    if (const auto* input = compileData.connectedResources.getField("color"))
        color.texture2D(input->getWidth(), input->getHeight()).format(input->getFormat());
    reflection.addInput("depth", "Scene depth").format(Falcor::ResourceFormat::D32Float).bindFlags(Falcor::ResourceBindFlags::DepthStencil);
    return reflection;
}

void SkyboxPass::compile(Falcor::RenderContext* context, const CompileData& compileData)
{
    auto&            device = *context->getDevice();
    const std::array descriptorSetLayouts{&device.sceneDescriptorSetLayout, &device.lightDescriptorSetLayout};
    const std::array pushConstantRanges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(SkyboxPushConstants),
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(descriptorSetLayouts, pushConstantRanges);

    pipeline = device.getOrCreatePipeline({
        .shader = context->shaderManager->getOrLoad("skybox"),
        .layout = pipelineLayout,
        .state =
            PipelineState{
                .vertexLayout = VertexLayoutPreset::None,
                .depthCompare = vk::CompareOp::eLessOrEqual,
                .depthWrite   = false
            },
        .colorFormats = {Falcor::getVulkanFormat(compileData.connectedResources.getField("color")->getFormat())},
        .depthFormat  = vk::Format::eD32Sfloat,
    });

    barriers.clear();
}

void SkyboxPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commandBuffer = *renderCommands->commands;
    const auto& execution     = *renderCommands;

    if (barriers.empty())
    {
        const auto depthHandle = cache.registerImage(renderData.getTexture("depth"));
        attachments.depth = RasterDepthAttachment{depthHandle};

        barriers.add(depthHandle, vulkan_access::kDepthRead);
        barriers.declare(cache);
    }

    const auto color = cache.registerImage(renderData.getTexture("color"));
    attachments.colors.front() = {color, vk::AttachmentLoadOp::eLoad};
    cache.transition(commandBuffer, color, vulkan_access::kColorReadWrite);
    barriers.transition(cache, commandBuffer);

    if (!execution.scene->environmentMap)
        return;

    const auto viewport = attachments.begin(cache, commandBuffer, execution.view->viewport).toScissor();
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array descriptorSets = {execution.frameData->sceneSetHandle(), execution.frameData->lightSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        descriptorSets,
        {}
    );
    const SkyboxPushConstants parameters{
        .viewport = {
            static_cast<float>(viewport.offset.x),
            static_cast<float>(viewport.offset.y),
            static_cast<float>(viewport.extent.width),
            static_cast<float>(viewport.extent.height),
        },
    };
    commandBuffer.pushConstants<SkyboxPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commandBuffer.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commandBuffer.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, SkyboxPass>();
}
