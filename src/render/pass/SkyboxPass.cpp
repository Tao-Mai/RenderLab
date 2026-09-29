#include "render/pass/SkyboxPass.h"

#include "render/present/Swapchain.h"

#include <array>

void SkyboxPass::init(Swapchain& targetSwapchain, PipelineManager& targetPipelines,
    ShaderHandle targetShader)
{
    swapchain = &targetSwapchain;
    pipelines = &targetPipelines;
    shader = targetShader;

    refreshPipeline();
}

void SkyboxPass::refreshPipeline()
{
    pipeline = pipelines->getOrCreate({
        .shader = shader,
        .layout = PipelineLayoutPreset::SceneOnly,
        .state = PipelineState::preset(RenderMode::Skybox),
        .colorFormat = swapchain->surfaceFormat().format,
        .depthFormat = swapchain->depthImageFormat(),
    });
    pipelineLayout = pipelines->layout(PipelineLayoutPreset::SceneOnly);
}

void SkyboxPass::reset() noexcept
{
    pipeline = nullptr;
    pipelineLayout = nullptr;
    shader = {};
    pipelines = nullptr;
    swapchain = nullptr;
}

void SkyboxPass::record(vk::raii::CommandBuffer& commandBuffer,
    vk::DescriptorSet sceneSet, const SkyboxPushConstants& parameters) const
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

    const std::array sets = {sceneSet};
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        pipelineLayout, RenderInterface::sceneSet, sets, {});
    commandBuffer.pushConstants<SkyboxPushConstants>(pipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, parameters);
    commandBuffer.draw(3, 1, 0, 0);
}
