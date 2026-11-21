#include "render/device/ResourceBarriers.h"
#include "passes/raster/DeferredLightingPass/DeferredLightingPass.h"

#include "core/Logger.h"
#include "core/profiling/Profiler.h"
#include "core/Plugin.h"

#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resources/NativeFormats.h"
#include "render/resources/ShaderData.h"

#include <array>
#include <utility>

namespace
{
constexpr std::string_view kDeferredLightingShader = "deferred_lighting";
constexpr std::array kGBufferChannels{
    std::pair{"diffuseOcclusion", RenderInterface::kGBufferDiffuseBinding},
    std::pair{"normalRoughness", RenderInterface::kGBufferNormalBinding},
    std::pair{"emissiveSpecularF90", RenderInterface::kGBufferEmissiveBinding},
    std::pair{"specularF0DiffuseFresnel", RenderInterface::kGBufferSpecularBinding},
    std::pair{"sheenColorRoughness", RenderInterface::kGBufferSheenBinding},
    std::pair{"depth", RenderInterface::kGBufferDepthBinding},
};
}

DeferredLightingPass::DeferredLightingPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<RasterLightingSettings>(properties))
{
}

Falcor::Properties DeferredLightingPass::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void DeferredLightingPass::setProperties(const Falcor::Properties& properties)
{
    RasterLightingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);

    const bool recompile = next.hdr != parameters.hdr;
    parameters = std::move(next);
    if (recompile)
        requestRecompile();
}

Falcor::RenderPassReflection DeferredLightingPass::reflect(const CompileData& compileData)
{
    const auto* depth = compileData.connectedResources.getField("depth");
    const auto extent = depth ? Falcor::uint2{depth->getWidth(), depth->getHeight()} : Falcor::uint2{};

    Falcor::RenderPassReflection reflection;
    for (const auto& channel : kGBufferChannels)
        reflection.addInput(channel.first, "G-buffer texture")
            .texture2D(0, 0, 1, 0, 0)
            .bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addOutput("color", "Raster lighting result")
        .format(parameters.getColorFormat(compileData.defaultTexFormat))
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget)
        .texture2D(extent.x, extent.y);
    return reflection;
}

void DeferredLightingPass::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    parameters.validateColorFormat(*passContext->vulkan);

    std::array<vk::DescriptorSetLayoutBinding, kGBufferChannels.size()> gBufferBindings;
    for (size_t index = 0; index < kGBufferChannels.size(); ++index)
    {
        gBufferBindings[index] = {
            .binding         = kGBufferChannels[index].second,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eFragment,
        };
    }
    auto& device             = *passContext->getDevice();
    descriptorSetLayout      = device.createDescriptorSetLayout(gBufferBindings);
    const std::array sets{
        &device.sceneDescriptorSetLayout,
        &device.lightDescriptorSetLayout,
        &descriptorSetLayout,
        &device.shadowDescriptorSetLayout
    };
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(DeferredLightingPushConstants)
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(sets, ranges);

    const Falcor::DefineList variant{
        {"ENABLE_IBL", passContext->config->feature.ibl ? "1" : "0"},
        {"ENABLE_CLEARCOAT", "0"},
        {"ENABLE_SHEEN", "1"}
    };
    pipeline      = device.getOrCreatePipeline({
             .shader = passContext->shaderManager->getOrLoad(kDeferredLightingShader, variant),
             .layout = pipelineLayout,
             .state  = PipelineState{.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
             .colorFormats = {Falcor::getVulkanFormat(parameters.getColorFormat(compileData.defaultTexFormat))},
    });
    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);

    barriers.clear();
}

void DeferredLightingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto&       cache         = *renderCommands->vulkanResources;
    auto&       commandBuffer = *renderCommands->commands;
    const auto& execution     = *renderCommands;

    if (barriers.empty())
    {
        constexpr auto kFragment = vk::PipelineStageFlagBits2::eFragmentShader;
        for (const auto& [name, binding] : kGBufferChannels)
        {
            const auto image = cache.registerImage(renderData.getTexture(name));
            cache.writeDescriptor(*descriptorSet, binding, image);
            barriers.add(image, vulkan_access::sampled(kFragment));
        }
        barriers.add(execution.frameData->lightBufferHandle(), vulkan_access::storageRead(kFragment));
        barriers.declare(cache);
    }

    barriers.transition(cache, commandBuffer);
    const auto color = cache.registerImage(renderData.getTexture("color"));
    attachments.colors.front() = {color, vk::AttachmentLoadOp::eDontCare};
    cache.transition(commandBuffer, color, vulkan_access::kColorWrite);

    auto& commands = commandBuffer;
    attachments.begin(cache, commands);
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sharedSets{execution.frameData->sceneSetHandle(), execution.frameData->lightSetHandle()};
    commands.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        sharedSets,
        {}
    );
    const std::array passSets{*descriptorSet, execution.frameData->shadowSetHandle()};
    commands
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, RenderInterface::kDeferredPassSet, passSets, {});
    const auto&                         viewport = execution.view->viewport;
    const DeferredLightingPushConstants parameters{
        .viewport = {viewport.x, viewport.y, viewport.width, viewport.height}
    };
    commands.pushConstants<DeferredLightingPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, DeferredLightingPass>();
}
