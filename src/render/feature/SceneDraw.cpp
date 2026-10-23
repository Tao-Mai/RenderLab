#include "render/feature/SceneDraw.h"
#include "core/Context.h"

#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/device/Device.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/MaterialTable.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
#include <array>

ShadowPass::Output
SceneDraw::useLightingResources(RenderGraph& graph, RenderGraph::BufferHandle lights, ShadowPass::Output shadow)
{
    const RenderGraph::MemoryAccess storageRead{
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderStorageRead,
    };
    graph.use(lights, storageRead);

    RenderGraph::ImageHandle fallback;
    const auto               use = [&](RenderGraph::ImageHandle image)
    {
        if (image.id == RenderGraph::kInvalidHandle)
        {
            if (fallback.id == RenderGraph::kInvalidHandle)
            {
                constexpr uint32_t kCubeFaceCount = 6;
                fallback                          = graph.createImage({
                                             .flags       = vk::ImageCreateFlagBits::eCubeCompatible,
                                             .format      = vk::Format::eD32Sfloat,
                                             .extent      = {1, 1, 1},
                                             .arrayLayers = kCubeFaceCount,
                });
            }
            image = fallback;
        }
        graph.use(
            image,
            {
                .stages = vk::PipelineStageFlagBits2::eFragmentShader,
                .access = vk::AccessFlagBits2::eShaderSampledRead,
                .layout = vk::ImageLayout::eShaderReadOnlyOptimal,
            }
        );
        return image;
    };
    RenderGraph::BufferHandle fallbackBuffer;
    const auto                useBuffer = [&](RenderGraph::BufferHandle buffer)
    {
        if (buffer.id == RenderGraph::kInvalidHandle)
        {
            if (fallbackBuffer.id == RenderGraph::kInvalidHandle)
                fallbackBuffer = graph.createBuffer({
                    .size = std::max(
                        {sizeof(PointShadowData),
                         sizeof(SpotShadowData),
                         sizeof(RectAreaShadowData),
                         sizeof(DirectionalShadowData)}
                    ),
                });
            buffer = fallbackBuffer;
        }
        graph.use(buffer, storageRead);
        return buffer;
    };
    return {
        use(shadow.pointLightShadowMap),
        use(shadow.spotLightShadowMap),
        use(shadow.rectAreaLightShadowMap),
        use(shadow.cascadeShadowMap),
        useBuffer(shadow.pointShadowBuffer),
        useBuffer(shadow.spotShadowBuffer),
        useBuffer(shadow.rectAreaShadowBuffer),
        useBuffer(shadow.directionalShadowBuffer),
    };
}

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
    const auto                               extent = graph.imageExtent(colors.front());
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
    std::span<const uint32_t>   drawOrder
)
{
    auto&            device         = *renderContext().device;
    auto&            shaders        = *renderContext().shaderManager;
    auto&            resources      = *renderContext().resources;
    const auto&      materials      = *renderContext().materialTable;
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
    const auto   itemCount = options.transparent ? drawOrder.size() : execution.renderData.drawItems.size();
    for (uint32_t orderIndex = 0; orderIndex < itemCount; ++orderIndex)
    {
        const uint32_t itemIndex = options.transparent ? drawOrder[orderIndex] : orderIndex;
        if (!execution.renderData.isVisible(itemIndex))
            continue;

        const auto& item          = execution.renderData.drawItems[itemIndex];
        const auto  materialIndex = execution.renderData.materialIndices[itemIndex];
        const auto& material      = materials.info(materialIndex);
        const bool  transparent   = material.alphaMode == kAlphaModeBlend;
        if (transparent != options.transparent)
            continue;

        const GraphicsPipelineInfo key{
            .shader = shader.index ? shader
                                   : shaders.getOrLoad(
                                         material.shaderId,
                                         {{"ENABLE_IBL", options.ibl ? 1 : 0},
                                          {"ENABLE_CLEARCOAT", material.clearcoat && options.clearcoat ? 1 : 0},
                                          {"ENABLE_SHEEN", material.sheen ? 1 : 0}}
                                     ),
            .layout = pipelineLayout,
            .state =
                {
                    .cullMode     = material.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack,
                    .depthCompare = transparent ? vk::CompareOp::eLess : vk::CompareOp::eEqual,
                    .depthWrite   = false,
                    .blend        = transparent,
                },
            .colorFormats = std::vector<vk::Format>(colorFormats.begin(), colorFormats.end()),
            .depthFormat  = vk::Format::eD32Sfloat,
        };
        const vk::Pipeline pipeline = device.getOrCreatePipeline(key);
        if (pipeline != boundPipeline)
        {
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
            boundPipeline = pipeline;
        }

        auto&       mesh    = resources.mesh(item.mesh);
        const auto& submesh = mesh.submeshes()[item.submeshIndex];
        mesh.bind(commandBuffer);
        const MeshPushConstants pushConstants{.model = item.model, .materialIndex = materialIndex};
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
