#include "render/postprocess/ExposurePass.h"

#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/GpuShader.h"

#include <glm/vec4.hpp>

namespace
{
constexpr uint32_t kHistogramBinCount      = 256;
constexpr uint32_t kHistogramWorkgroupSize = 16;
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

ExposurePass::Output ExposurePass::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle hdrColor)
{
    source        = hdrColor;
    extent        = graph.imageInfo(source).extent;
    hasHistory    = false;
    previousScene = {};

    graph.use(
        source,
        {
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }
    );
    histogram = graph.write(graph.createBuffer({
        .size  = kHistogramBinCount * sizeof(uint32_t),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    }));
    graph.use(
        histogram,
        {
            .stages = vk::PipelineStageFlagBits2::eTransfer | vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eTransferWrite | vk::AccessFlagBits2::eShaderStorageRead |
                vk::AccessFlagBits2::eShaderStorageWrite,
        }
    );
    output.state = graph.write(graph.createBuffer({.size = sizeof(State)}));
    graph.use(
        output.state,
        {
            .stages = vk::PipelineStageFlagBits2::eComputeShader,
            .access = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        }
    );

    auto& device = *renderContext().device;
    if (!*pipelines[0])
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

        constexpr std::array kShaderPaths{"shaders/exposure.histogram.spv", "shaders/exposure.resolve.spv"};
        for (size_t index = 0; index < pipelines.size(); ++index)
        {
            GpuShader shader{setup.vulkan.deviceHandle(), kShaderPaths[index]};
            pipelines[index] = vkCheck(setup.vulkan.deviceHandle().createComputePipeline(
                vk::raii::PipelineCache{nullptr},
                {
                    .stage =
                        {.stage = vk::ShaderStageFlagBits::eCompute, .module = shader.handle(), .pName = "computeMain"},
                    .layout = pipelineLayout,
                }
            ));
        }
    }

    descriptorSet = device.allocateDescriptorSet(descriptorSetLayout);
    return output;
}

void ExposurePass::writeDescriptorSet(RenderGraph& graph, const PassSetup& setup) const
{
    const vk::DescriptorImageInfo image{
        .imageView   = graph.getImage(source).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.imageInfo(source).format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const std::array buffers{
        vk::DescriptorBufferInfo{
            .buffer = graph.getBuffer(histogram).handle(),
            .range  = graph.getBuffer(histogram).size()
        },
        vk::DescriptorBufferInfo{.buffer = graph.getBuffer(output.state).handle(), .range = sizeof(State)},
    };
    const std::array writes{
        vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = kSourceBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eSampledImage,
            .pImageInfo      = &image,
        },
        vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = kHistogramBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &buffers[0],
        },
        vk::WriteDescriptorSet{
            .dstSet          = *descriptorSet,
            .dstBinding      = kStateBinding,
            .descriptorCount = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .pBufferInfo     = &buffers[1],
        },
    };
    setup.vulkan.deviceHandle().updateDescriptorSets(writes, {});
}

void ExposurePass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame)
{
    const auto&              settings  = frame.renderData.exposure;
    const auto&              automatic = settings.automatic;
    const auto&              viewport  = frame.view.viewport;
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
        .historyValid          = hasHistory && previousScene == frame.renderData.sceneId ? 1u : 0u,
    };
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayout, 0, std::array{*descriptorSet}, {});
    commands.pushConstants<ExposureParameters>(pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, parameters);

    if (enabled)
    {
        const auto buffer = graph.getBuffer(histogram).handle();
        commands.fillBuffer(buffer, 0, VK_WHOLE_SIZE, 0);
        vk::BufferMemoryBarrier2 barrier{
            .srcStageMask        = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask        = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask       = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = buffer,
            .size                = VK_WHOLE_SIZE,
        };
        commands.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
        commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipelines[0]);
        constexpr uint32_t kPixelsPerGroup = kHistogramWorkgroupSize * kMeteringStride;
        commands.dispatch(
            (extent.width + kPixelsPerGroup - 1) / kPixelsPerGroup,
            (extent.height + kPixelsPerGroup - 1) / kPixelsPerGroup,
            1
        );

        barrier.srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader;
        barrier.srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite;
        barrier.dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead;
        commands.pipelineBarrier2({.bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &barrier});
    }

    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipelines[1]);
    commands.dispatch(1, 1, 1);
    hasHistory    = true;
    previousScene = frame.renderData.sceneId;
}
