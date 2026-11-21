#pragma once

#include "render/denoise/DenoiserInputs.h"
#include "render/graph/RenderPass.h"
#include "render/resources/ShaderLayout.h"

#include <span>
#include <vector>

class RayTracingStorageImages
{
public:
    struct Outputs
    {
        RenderGraph::ImageHandle rayImage;
        DenoiserInputs denoiser;

        void publish(PassResources& ports) const;
    };

    static void reflect(RenderPassReflection& reflection);
    [[nodiscard]] Outputs compile(
        RenderGraph& graph, const PassSetup& setup, const ShaderLayout& shader, bool splitLighting
    );
    void appendLayoutBindings(std::vector<vk::DescriptorSetLayoutBinding>& bindings) const;
    void writeDescriptors(RenderGraph& graph, const PassSetup& setup, std::span<const vk::raii::DescriptorSet> sets) const;

private:
    struct Binding
    {
        ShaderBinding<vk::DescriptorType::eStorageImage> binding;
        RenderGraph::ImageHandle image;
    };
    std::vector<Binding> images;

    [[nodiscard]] RenderGraph::ImageHandle create(
        RenderGraph& graph, const PassSetup& setup, const ShaderStorageImage& target,
        vk::FormatFeatureFlags additionalFeatures
    );
};
