#include "render/device/ResourceBarriers.h"
#include "passes/postprocess/ExposurePass/ExposurePass.h"

#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/RenderView.h"

#include <array>
#include <glm/vec4.hpp>

namespace
{
constexpr char     kHistogram[]            = "histogram";
constexpr uint32_t kHistogramBinCount      = 256;
constexpr uint32_t kMeteringStride         = 2;
constexpr uint32_t kSourceBinding          = 0;
constexpr uint32_t kHistogramBinding       = 1;
constexpr uint32_t kStateBinding           = 2;

struct ExposureParameters
{
    glm::vec4 viewport;
    float     deltaSeconds;
    float     manualEv100;
    float     exposureCompensation;
    uint32_t  automatic;
    float     minEv100;
    float     maxEv100;
    float     brightAdaptationSpeed;
    float     darkAdaptationSpeed;
    float     lowPercentile;
    float     highPercentile;
    uint32_t  historyValid;
    uint32_t  padding = 0;
};
static_assert(sizeof(ExposureParameters) == 64);
}  // namespace

ExposurePass::ExposurePass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
    Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<ExposurePassSettings>(properties))
{
    parameters.validate();
}

Falcor::Properties ExposurePass::getProperties() const
{
    return Falcor::serializeToProperties(parameters);
}

void ExposurePass::setProperties(const Falcor::Properties& properties)
{
    ExposurePassSettings next;
    Deserialize(nlohmann::json(properties.toJson()), next);
    next.validate();

    parameters = std::move(next);
}

Falcor::RenderPassReflection ExposurePass::reflect(const CompileData&)
{
    Falcor::RenderPassReflection reflection;
    reflection.addInput("hdrColor", "HDR scene color").bindFlags(Falcor::ResourceBindFlags::ShaderResource);
    reflection.addOutput(kExposureStateOutput, "Exposure state")
        .rawBuffer(sizeof(ExposureState))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess)
        .flags(Falcor::RenderPassReflection::Field::Flags::Persistent);
    reflection.addInternal(kHistogram, "Luminance histogram")
        .rawBuffer(kHistogramBinCount * sizeof(uint32_t))
        .bindFlags(Falcor::ResourceBindFlags::UnorderedAccess);
    return reflection;
}

void ExposurePass::compile(Falcor::RenderContext* context, const CompileData&)
{
    hasHistory    = false;
    previousScene = {};

    auto& device = *context->getDevice();
    if (!*histogramPipeline)
    {
        const std::array bindings{
            vk::DescriptorSetLayoutBinding{
                .binding         = kSourceBinding,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eCompute,
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = kHistogramBinding,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eCompute,
            },
            vk::DescriptorSetLayoutBinding{
                .binding         = kStateBinding,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .descriptorCount = 1,
                .stageFlags      = vk::ShaderStageFlagBits::eCompute,
            },
        };
        descriptorSetLayout = device.createDescriptorSetLayout(bindings);
        const std::array layouts{&descriptorSetLayout};
        const std::array ranges{vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .size       = sizeof(ExposureParameters),
        }};
        pipelineLayout = device.getOrCreatePipelineLayout(layouts, ranges);

        constexpr char kHistogramStage[] = "0";
        constexpr char kResolveStage[] = "1";
        auto& shaders = *context->shaderManager;
        histogramPipeline = device.createComputePipeline(
            shaders.getOrLoad("exposure", {{"EXPOSURE_STAGE", kHistogramStage}}), pipelineLayout
        );
        resolvePipeline = device.createComputePipeline(
            shaders.getOrLoad("exposure", {{"EXPOSURE_STAGE", kResolveStage}}), pipelineLayout
        );
    }

    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
    barriers.clear();
}

void ExposurePass::execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData)
{
    constexpr auto kCompute = vk::PipelineStageFlagBits2::eComputeShader;
    constexpr VulkanResources::MemoryAccess kStorageReadWrite{
        kCompute,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
    };

    auto&       cache    = *renderCommands->vulkanResources;
    auto&       commands = *renderCommands->commands;
    const auto& frame    = *renderCommands;
    if (barriers.empty())
    {
        const auto source = cache.registerImage(renderData.getTexture("hdrColor"));
        const auto exposureState = cache.registerBuffer(renderData[kExposureStateOutput]->asBuffer());
        histogram = cache.registerBuffer(renderData[kHistogram]->asBuffer());
        extent = cache.imageInfo(source).extent;
        cache.writeDescriptor(*descriptorSet, kSourceBinding, source);
        cache.writeDescriptor(*descriptorSet, kHistogramBinding, histogram);
        cache.writeDescriptor(*descriptorSet, kStateBinding, exposureState);

        barriers.add(source, vulkan_access::sampled(kCompute));
        barriers.add(exposureState, kStorageReadWrite);
        barriers.declare(cache);
    }

    barriers.transition(cache, commands);

    const ExposureConfig&    settings  = frame.scene->exposure ? *frame.scene->exposure : parameters;
    const auto&              automatic = settings.automatic;
    const auto&              viewport  = frame.view->viewport;
    const bool               enabled   = settings.mode == ExposureMode::Automatic;
    const ExposureParameters parameters{
        .viewport              = {viewport.x, viewport.y, viewport.width, viewport.height},
        .deltaSeconds          = frame.deltaSeconds,
        .manualEv100           = settings.ev100,
        .exposureCompensation  = settings.exposureCompensation,
        .automatic             = enabled ? 1u : 0u,
        .minEv100              = automatic.minEv100,
        .maxEv100              = automatic.maxEv100,
        .brightAdaptationSpeed = automatic.brightAdaptationSpeed,
        .darkAdaptationSpeed   = automatic.darkAdaptationSpeed,
        .lowPercentile         = automatic.lowPercentile,
        .highPercentile        = automatic.highPercentile,
        .historyValid          = hasHistory && previousScene == frame.scene->sceneId ? 1u : 0u,
    };
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, std::array{*descriptorSet}, {});
    commands.pushConstants<ExposureParameters>(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, parameters);

    if (enabled)
    {
        renderCommands->clearUAV(cache.getBuffer(histogram).getUAV().get(), Falcor::uint4(0));
        cache.transition(commands, histogram, kStorageReadWrite);
        commands.bindPipeline(vk::PipelineBindPoint::eCompute, *histogramPipeline);
        const auto meteringThreads = [](uint32_t pixels) { return (pixels + kMeteringStride - 1) / kMeteringStride; };
        histogramPipeline.dispatch(commands, {meteringThreads(extent.width), meteringThreads(extent.height), 1});
    }

    cache.transition(commands, histogram, vulkan_access::storageRead(kCompute));
    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *resolvePipeline);
    commands.dispatch(1, 1, 1);
    hasHistory    = true;
    previousScene = frame.scene->sceneId;
}
