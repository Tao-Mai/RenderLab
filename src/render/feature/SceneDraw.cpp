#include "render/feature/SceneDraw.h"
#include "render/feature/SceneInterface.h"

#include "render/FrameData.h"
#include "render/PipelineManager.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
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
    CommandRecorder&                          commandBuffer,
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
            .imageView = graph.getImage(color, execution.frameIndex)
                             .getOrCreateImageView({
                                 .viewType = vk::ImageViewType::e2D,
                                 .format   = graph.imageInfo(color).format,
                                 .subresourceRange =
                                     {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
                             }),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = colorLoadOp,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f),
        });
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView = graph.getImage(depth, execution.frameIndex)
                         .getOrCreateImageView({
                             .viewType = vk::ImageViewType::e2D,
                             .format   = vk::Format::eD32Sfloat,
                             .subresourceRange =
                                 {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
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

    const auto&    editorViewport = execution.view.viewport;
    const uint32_t viewportX      = std::min(static_cast<uint32_t>(std::max(editorViewport.x, 0.0f)), extent.width - 1);
    const uint32_t viewportY = std::min(static_cast<uint32_t>(std::max(editorViewport.y, 0.0f)), extent.height - 1);
    const uint32_t viewportWidth =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.width), extent.width - viewportX));
    const uint32_t viewportHeight =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.height), extent.height - viewportY));
    const vk::Rect2D viewport{
        {static_cast<int32_t>(viewportX), static_cast<int32_t>(viewportY)},
        {viewportWidth, viewportHeight},
    };
    commandBuffer.setViewport(
        0,
        vk::Viewport(
            static_cast<float>(viewportX),
            static_cast<float>(viewportY),
            static_cast<float>(viewportWidth),
            static_cast<float>(viewportHeight),
            0.0f,
            1.0f
        )
    );
    commandBuffer.setScissor(0, viewport);
    return viewport;
}

void SceneDraw::recordMeshes(
    CommandRecorder&            commandBuffer,
    const PassFrame&            execution,
    std::span<const DrawItem>   items,
    std::span<const vk::Format> colorFormats,
    ShaderHandle                shader,
    SceneShaderOptions          options
)
{
    auto&      pipelines = *renderContext().pipelineManager;
    auto&      shaders   = *renderContext().shaderManager;
    const auto pipelineLayout =
        SceneInterface::meshLayout(*renderContext().descriptors, *renderContext().pipelineManager);
    const std::array sceneSets = {execution.data.sceneSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kSceneSet,
        sceneSets,
        {}
    );

    vk::Pipeline boundPipeline;
    for (const DrawItem& item : items)
    {
        PipelineKey key      = item.material->pipelineKey(colorFormats, vk::Format::eD32Sfloat, pipelineLayout);
        key.state.depthWrite = false;
        if (item.material->alphaMode() != MaterialAlphaMode::Blend)
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
        const vk::Pipeline pipeline = pipelines.getOrCreate(key);
        if (pipeline != boundPipeline)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            boundPipeline = pipeline;
        }

        item.mesh->bind(commandBuffer);
        const MeshPushConstants pushConstants{.model = item.model};
        commandBuffer.pushConstants<MeshPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants
        );
        const std::array materialSets = {item.material->descriptorSetHandle()};
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipelineLayout,
            RenderInterface::kMaterialSet,
            materialSets,
            {}
        );
        commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
    }
}
