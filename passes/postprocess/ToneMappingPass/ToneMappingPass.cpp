#include "render/device/ResourceBarriers.h"
#include "passes/postprocess/ToneMappingPass/ToneMappingPass.h"

#include "core/Logger.h"
#include "core/profiling/Profiler.h"
#include "editor/PassEditor.h"

#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resources/NativeFormats.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/ShaderData.h"

#include <array>

ToneMappingPass::ToneMappingPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<ToneMappingSettings>(properties))
{
    parameters.validate();
}

Falcor::Properties ToneMappingPass::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void ToneMappingPass::setProperties(const Falcor::Properties& properties)
{
    ToneMappingSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}

Falcor::RenderPassReflection ToneMappingPass::reflect(const CompileData& compileData)
{
    Falcor::RenderPassReflection reflection;
    reflection.addInput("hdrColor", "HDR scene color").bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addInput("exposure", "Exposure state").rawBuffer(0).bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addOutput("color", "Tone-mapped color")
        .format(compileData.defaultTexFormat)
        .bindFlags(Falcor::ResourceBindFlags::RenderTarget);
    return reflection;
}

void ToneMappingPass::compile(Falcor::RenderContext* passContext, const CompileData& compileData)
{
    const auto targetFormat   = Falcor::getVulkanFormat(compileData.defaultTexFormat);
    const bool srgbAttachment = targetFormat == vk::Format::eB8G8R8A8Srgb || targetFormat == vk::Format::eR8G8B8A8Srgb;
    CHECK(
        srgbAttachment || targetFormat == vk::Format::eB8G8R8A8Unorm || targetFormat == vk::Format::eR8G8B8A8Unorm,
        "unsupported tone mapping output format: {}",
        vk::to_string(targetFormat)
    );
    encodeSrgb = !srgbAttachment;

    const std::array toneMappingBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kToneMappingImageBinding,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kToneMappingSamplerBinding,
            .descriptorType  = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = RenderInterface::kToneMappingExposureBinding,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eFragment,
        },
    };
    auto& device        = *passContext->getDevice();
    descriptorSetLayout = device.createDescriptorSetLayout(toneMappingBindings);
    const std::array sets{&descriptorSetLayout};
    const std::array ranges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(ToneMappingPushConstants)
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(sets, ranges);

    const auto shader = passContext->shaderManager->getOrLoad("tonemapping");
    pipeline          = device.getOrCreatePipeline({
                 .shader = shader,
                 .layout = pipelineLayout,
                 .state  = PipelineState{.vertexLayout = VertexLayoutPreset::None, .depthTest = false, .depthWrite = false},
                 .colorFormats = {targetFormat},
    });
    const auto sampler = passContext->resources->sampler(BuiltinAssets::Sampler::linearClamp);

    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);

    passContext->vulkanResources->writeDescriptor(*descriptorSet, RenderInterface::kToneMappingSamplerBinding, sampler);
    barriers.clear();
}

void ToneMappingPass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    auto& cache    = *renderCommands->vulkanResources;
    auto& commands = *renderCommands->commands;
    if (barriers.empty())
    {
        const auto hdrColor = cache.registerImage(renderData.getTexture("hdrColor"));
        const auto exposure = cache.registerBuffer(renderData["exposure"]->asBuffer());
        cache.writeDescriptor(*descriptorSet, RenderInterface::kToneMappingImageBinding, hdrColor);
        cache.writeDescriptor(*descriptorSet, RenderInterface::kToneMappingExposureBinding, exposure);

        constexpr auto kFragment = vk::PipelineStageFlagBits2::eFragmentShader;
        barriers.add(hdrColor, vulkan_access::sampled(kFragment));
        barriers.add(exposure, vulkan_access::storageRead(kFragment));
        barriers.declare(cache);
    }

    barriers.transition(cache, commands);
    const auto color = cache.registerImage(renderData.getTexture("color"));
    attachments.colors.front() = {color, vk::AttachmentLoadOp::eDontCare};
    cache.transition(commands, color, vulkan_access::kColorWrite);

    attachments.begin(cache, commands);
    commands.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
    const std::array sets = {*descriptorSet};
    commands.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, sets, {});

    const auto&                    settings = parameters;
    const ToneMappingPushConstants parameters{
        .mode       = static_cast<uint32_t>(settings.mode),
        .encodeSrgb = encodeSrgb ? 1u : 0u,
    };
    commands.pushConstants<ToneMappingPushConstants>(
        pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commands.draw(3, 1, 0, 0);
    recordDraw(context().profiler, 3);
    commands.endRendering();
}

void ToneMappingPass::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor(widget, *this, parameters, context->passEditor, "Tone mapping");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, ToneMappingPass>();
}
