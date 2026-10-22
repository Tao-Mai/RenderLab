#include "render/feature/SceneDraw.h"
#include "core/Context.h"

#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <array>

void SceneDraw::useAttachments(
    RenderGraph& graph, std::span<const RenderGraph::ImageHandle> colors, RenderGraph::ImageHandle depth
)
{
    for (const auto color : colors)
        graph.use(
            color,
            {
                .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .access = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                .layout = vk::ImageLayout::eColorAttachmentOptimal,
            }
        );
    graph.use(
        depth,
        {
            .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
            .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead,
            .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
        }
    );
}

vk::Rect2D SceneDraw::beginRendering(
    RenderGraph&                              graph,
    vk::raii::CommandBuffer&                  commandBuffer,
    const PassFrame&                          execution,
    std::span<const RenderGraph::ImageHandle> colors,
    RenderGraph::ImageHandle                  depth,
    vk::AttachmentLoadOp                      colorLoadOp
)
{
    const auto                               extent = execution.setup.swapchain.extent();
    std::vector<vk::RenderingAttachmentInfo> colorAttachments;
    colorAttachments.reserve(colors.size());
    for (const auto color : colors)
        colorAttachments.push_back({
            .imageView   = graph.getImage(color).getOrCreateImageView({
                  .viewType         = vk::ImageViewType::e2D,
                  .format           = graph.imageInfo(color).format,
                  .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
            }),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = colorLoadOp,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f),
        });
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = graph.getImage(depth).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = vk::Format::eD32Sfloat,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        .loadOp      = vk::AttachmentLoadOp::eLoad,
        .storeOp     = vk::AttachmentStoreOp::eNone,
    };
    commandBuffer.beginRendering({
        .renderArea           = {.offset = {0, 0}, .extent = extent},
        .layerCount           = 1,
        .colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size()),
        .pColorAttachments    = colorAttachments.data(),
        .pDepthAttachment     = &depthAttachment,
    });

    const auto viewport = execution.view.viewport.clamped(extent);
    commandBuffer.setViewport(0, viewport.toViewport());
    commandBuffer.setScissor(0, viewport.toScissor());
    return viewport.toScissor();
}

void SceneDraw::recordMeshes(
    vk::raii::CommandBuffer&    commandBuffer,
    const PassFrame&            execution,
    std::span<const vk::Format> colorFormats,
    ShaderHandle                shader,
    SceneDrawOptions            options,
    std::span<const uint32_t>   objectOrder
)
{
    auto&            device         = *renderContext().device;
    auto&            shaders        = *renderContext().shaderManager;
    auto&            resources      = *renderContext().resources;
    const auto&      pipelineLayout = renderContext().device->meshPipelineLayout;
    const std::array descriptorSets =
        {execution.data.sceneSetHandle(), execution.data.lightingSetHandle(), execution.renderData.materialSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        descriptorSets,
        {}
    );

    vk::Pipeline boundPipeline;
    const auto   objectCount = options.transparent ? objectOrder.size() : execution.renderData.objects.size();
    for (uint32_t orderIndex = 0; orderIndex < objectCount; ++orderIndex)
    {
        const uint32_t objectIndex = options.transparent ? objectOrder[orderIndex] : orderIndex;
        if (!execution.renderData.isVisible(objectIndex))
            continue;

        const auto& object = execution.renderData.objects[objectIndex];
        auto&       mesh   = resources.mesh(object.mesh);
        mesh.bind(commandBuffer);
        for (uint32_t index = 0; index < mesh.submeshes().size(); ++index)
        {
            const auto& submesh = mesh.submeshes()[index];
            const auto& binding = execution.renderData.material(object, index);
            const auto& material = *binding.material;
            if ((material.alphaMode() == MaterialAlphaMode::Blend) != options.transparent)
                continue;

            GraphicsPipelineInfo key = material.pipelineInfo(colorFormats, vk::Format::eD32Sfloat, pipelineLayout);
            key.state.depthWrite     = false;
            if (material.alphaMode() != MaterialAlphaMode::Blend)
                key.state.depthCompare = vk::CompareOp::eEqual;
            if (shader.index)
            {
                key.shader = shader;
            }
            else
            {
                const auto& metadata  = shaders.metadata(key.shader);
                const bool  clearcoat = metadata.defines.contains("ENABLE_CLEARCOAT") &&
                    metadata.defines.at("ENABLE_CLEARCOAT") && options.clearcoat;
                key.shader = shaders.getOrLoad(
                    metadata.id,
                    {{"ENABLE_IBL", options.ibl ? 1 : 0},
                     {"ENABLE_CLEARCOAT", clearcoat ? 1 : 0},
                     {"ENABLE_SHEEN", metadata.defines.at("ENABLE_SHEEN")}}
                );
            }
            const vk::Pipeline pipeline = device.getOrCreatePipeline(key);
            if (pipeline != boundPipeline)
            {
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
                boundPipeline = pipeline;
            }

            const MeshPushConstants pushConstants{.model = object.model, .materialIndex = binding.materialIndex};
            commandBuffer.pushConstants<MeshPushConstants>(
                pipelineLayout,
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                0,
                pushConstants
            );
            commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
            recordDraw(context().profiler, submesh.indexCount);
        }
    }
}
