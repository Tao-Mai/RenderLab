#include "render/feature/shadow/ShadowPass.h"

#include "core/Context.h"
#include "render/FrameData.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/RenderData.h"
#include "render/RenderView.h"
#include "render/RendererConfig.h"
#include "render/ShaderManager.h"
#include "render/Visibility.h"
#include "render/device/Device.h"
#include "render/device/VulkanContext.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace
{
struct ShadowVolume
{
    uint32_t             mask;
    std::vector<Frustum> frusta;
    uint32_t             visibleDrawItems = 0;
};

std::vector<ShadowVolume> shadowVolumes(const ShadowFrameData& lighting, uint32_t cascadeCount)
{
    std::vector<ShadowVolume> volumes;
    for (uint32_t index = 0; index < lighting.pointShadows.size(); ++index)
    {
        ShadowVolume volume{RenderVisibility::shadowMask(GpuLightType::Point, index), {}};
        for (const auto& vp : lighting.pointShadows[index].vp)
            volume.frusta.emplace_back(vp);
        volumes.push_back(std::move(volume));
    }
    for (uint32_t index = 0; index < lighting.spotShadows.size(); ++index)
        volumes.push_back(
            {RenderVisibility::shadowMask(GpuLightType::Spot, index), {Frustum{lighting.spotShadows[index].vp}}}
        );
    for (uint32_t index = 0; index < lighting.rectAreaShadows.size(); ++index)
        volumes.push_back(
            {RenderVisibility::shadowMask(GpuLightType::RectArea, index), {Frustum{lighting.rectAreaShadows[index].vp}}}
        );
    // Cascades depth-clamp upstream casters; the far plane still bounds relevant geometry.
    for (uint32_t index = 0; index < lighting.directionalShadows.size(); ++index)
        for (uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
            volumes.push_back(
                {RenderVisibility::shadowMask(GpuLightType::Directional, index, cascade),
                 {Frustum{lighting.directionalShadows[index].cascades[cascade].vp, Frustum::NearPlane::Exclude}}}
            );
    return volumes;
}

}  // namespace

ShadowPass::Output ShadowPass::setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::BufferHandle lights)
{
    lightBuffer = lights;
    const RenderGraph::MemoryAccess shaderRead{
        .stages = vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderStorageRead,
    };
    graph.use(lightBuffer, shaderRead);
    const auto& shadow = setup.config.feature.shadow;
    cascadeCount       = shadow.cascade.count;

    CHECK(cascadeCount > 0 && cascadeCount <= kMaxDirectionalShadowCascades);
    const auto& limits = setup.vulkan.properties().limits;
    CHECK(
        shadow.cascade.shadowMapSize <= limits.maxImageDimension2D &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferWidth &&
        shadow.cascade.shadowMapSize <= limits.maxFramebufferHeight &&
        kMaxDirectionalLightShadow * cascadeCount <= limits.maxImageArrayLayers
    );
    constexpr auto kRequiredFeatures = vk::FormatFeatureFlagBits::eDepthStencilAttachment |
        vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
    const auto features =
        setup.vulkan.physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures;
    CHECK((features & kRequiredFeatures) == kRequiredFeatures);

    // pipeline layout
    auto&            device = *renderContext().device;
    const std::array descriptorSetLayouts{&device.sceneDescriptorSetLayout, &device.lightingDescriptorSetLayout};
    const std::array pushConstantRanges{vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .size       = sizeof(ShadowPushConstants),
    }};
    pipelineLayout = device.getOrCreatePipelineLayout(descriptorSetLayouts, pushConstantRanges);

    const auto createTarget = [&](const RenderGraph::ImageCreateInfo& info,
                                  int                                 shadowType,
                                  uint32_t                            viewMask       = 0,
                                  uint32_t                            viewLayerCount = 1)
    {
        const auto shader   = renderContext().shaderManager->getOrLoad("shadow", {{"SHADOW_TYPE", shadowType}});
        const auto pipeline = device.getOrCreatePipeline({
            .shader = shader,
            .layout = pipelineLayout,
            .state =
                PipelineState{
                    .vertexLayout = VertexLayoutPreset::PositionOnly,
                    .depthClamp   = shadowType == static_cast<int>(GpuLightType::Directional),
                },
            .depthFormat = vk::Format::eD32Sfloat,
            .viewMask    = viewMask,
        });

        auto image = graph.createImage(info);
        image      = graph.write(image);
        graph.use(
            image,
            {
                .stages =
                    vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                .layout = vk::ImageLayout::eDepthAttachmentOptimal,
            }
        );

        return ShadowTarget{
            .image          = image,
            .pipeline       = pipeline,
            .mapSize        = info.extent.width,
            .viewMask       = viewMask,
            .viewLayerCount = viewLayerCount,
        };
    };

    point = createTarget(
        {
            .flags       = vk::ImageCreateFlagBits::eCubeCompatible,
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.pointShadowMapSize, shadow.pointShadowMapSize, 1},
            .arrayLayers = kPointFaceCount * kMaxPointLightShadow,
        },
        0,
        kPointViewMask,
        kPointFaceCount
    );
    spot = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.spotShadowMapSize, shadow.spotShadowMapSize, 1},
            .arrayLayers = kMaxSpotLightShadow,
        },
        static_cast<int>(GpuLightType::Spot)
    );
    rectArea = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.rectAreaShadowMapSize, shadow.rectAreaShadowMapSize, 1},
            .arrayLayers = kMaxRectAreaLightShadow,
        },
        2
    );
    directional = createTarget(
        {
            .format      = vk::Format::eD32Sfloat,
            .extent      = {shadow.cascade.shadowMapSize, shadow.cascade.shadowMapSize, 1},
            .arrayLayers = kMaxDirectionalLightShadow * cascadeCount,
        },
        1
    );

    const auto createBuffer = [&](vk::DeviceSize size)
    {
        const auto buffer = graph.write(graph.createBuffer({
            .size  = size,
            .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        }));
        graph.use(buffer, shaderRead);
        return buffer;
    };
    point.buffer       = createBuffer(kMaxPointLightShadow * sizeof(PointShadowData));
    spot.buffer        = createBuffer(kMaxSpotLightShadow * sizeof(SpotShadowData));
    rectArea.buffer    = createBuffer(kMaxRectAreaLightShadow * sizeof(RectAreaShadowData));
    directional.buffer = createBuffer(kMaxDirectionalLightShadow * sizeof(DirectionalShadowData));
    return {
        point.image,
        spot.image,
        rectArea.image,
        directional.image,
        point.buffer,
        spot.buffer,
        rectArea.buffer,
        directional.buffer,
    };
}

void ShadowPass::prepare(RenderGraph& graph, const PassFrame& frame)
{
    CpuProfileScope timing{context().profiler, "Shadow preparation"};
    shadowData   = prepareShadows(frame.view.uniforms, frame.renderData.lights, frame.setup.config.feature.shadow);
    auto volumes = shadowVolumes(shadowData, cascadeCount);
    visibilityMasks.clear();
    visibilityMasks.reserve(frame.renderData.drawBounds.size());

    for (const auto& bounds : frame.renderData.drawBounds)
    {
        uint32_t mask = 0;
        for (auto& volume : volumes)
        {
            if (!std::ranges::any_of(volume.frusta, [&](const Frustum& frustum) { return frustum.intersects(bounds); }))
                continue;

            mask |= volume.mask;
            ++volume.visibleDrawItems;
        }
        visibilityMasks.push_back(mask);
    }

    if (auto* profiler = context().profiler)
    {
        std::vector<ViewCullingStatistics> views;
        for (const auto& volume : volumes)
            views.push_back({RenderVisibility::viewName(volume.mask), volume.visibleDrawItems});
        profiler->recordCulling(static_cast<uint32_t>(frame.renderData.drawBounds.size()), std::move(views));
    }

    const auto upload = [&](RenderGraph::BufferHandle buffer, const auto& values)
    {
        if (values.empty())
            return;
        const auto bytes      = values.size() * sizeof(values.front());
        const auto allocation = graph.allocateUpload(buffer, bytes);
        std::memcpy(allocation.data.data(), values.data(), bytes);
    };
    upload(lightBuffer, shadowData.lights);
    upload(point.buffer, shadowData.pointShadows);
    upload(spot.buffer, shadowData.spotShadows);
    upload(rectArea.buffer, shadowData.rectAreaShadows);
    upload(directional.buffer, shadowData.directionalShadows);
}

void ShadowPass::execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
{
    const auto& lights = shadowData.lights;
    const auto& ranges = execution.renderData.lightRanges;

    for (uint32_t index = 0; index < ranges.directionalEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowidx < shadowData.directionalShadows.size());
        for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
            recordShadow(
                graph,
                commandBuffer,
                execution,
                directional,
                index,
                light.shadowidx * cascadeCount + cascadeIndex,
                cascadeIndex
            );
    }

    for (uint32_t index = ranges.directionalEnd; index < ranges.pointEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowidx < shadowData.pointShadows.size());
        recordShadow(graph, commandBuffer, execution, point, index, light.shadowidx * kPointFaceCount);
    }

    for (uint32_t index = ranges.pointEnd; index < ranges.rectAreaEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowidx < shadowData.rectAreaShadows.size());
        recordShadow(graph, commandBuffer, execution, rectArea, index, light.shadowidx);
    }

    for (uint32_t index = ranges.rectAreaEnd; index < ranges.spotEnd; ++index)
    {
        const auto& light = lights[index];
        if (!light.castShadow())
            continue;

        DCHECK(light.shadowidx < shadowData.spotShadows.size());
        recordShadow(graph, commandBuffer, execution, spot, index, light.shadowidx);
    }
}

void ShadowPass::recordShadow(
    RenderGraph&             graph,
    vk::raii::CommandBuffer& commandBuffer,
    const PassFrame&         execution,
    const ShadowTarget&      target,
    uint32_t                 lightIndex,
    uint32_t                 baseArrayLayer,
    uint32_t                 cascadeIndex
) const
{
    // TODO 提前算
    const auto imageView =
        graph.getImage(target.image)
            .getOrCreateImageView({
                .viewType         = target.viewLayerCount > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D,
                .format           = vk::Format::eD32Sfloat,
                .subresourceRange = {
                    .aspectMask     = vk::ImageAspectFlagBits::eDepth,
                    .levelCount     = 1,
                    .baseArrayLayer = baseArrayLayer,
                    .layerCount     = target.viewLayerCount,
                },
            });
    const vk::RenderingAttachmentInfo depthAttachment{
        .imageView   = imageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = vk::ClearDepthStencilValue(1.0f, 0),
    };

    commandBuffer.beginRendering({
        .renderArea       = {{0, 0}, {target.mapSize, target.mapSize}},
        .layerCount       = 1,
        .viewMask         = target.viewMask,
        .pDepthAttachment = &depthAttachment,
    });

    // Multiview resets graphics state at the beginning of each rendering instance.
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, target.pipeline);
    const std::array lightingSets = {execution.data.lightingSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::kLightingSet,
        lightingSets,
        {}
    );
    commandBuffer.setViewport(
        0,
        vk::Viewport(0.0f, 0.0f, static_cast<float>(target.mapSize), static_cast<float>(target.mapSize), 0.0f, 1.0f)
    );
    commandBuffer.setScissor(0, vk::Rect2D({0, 0}, {target.mapSize, target.mapSize}));

    const auto& light      = shadowData.lights[lightIndex];
    const auto  visibility = RenderVisibility::shadowMask(light.type(), light.shadowidx, cascadeIndex);
    for (size_t index = 0; index < execution.renderData.drawItems.size(); ++index)
    {
        if (!(visibilityMasks[index] & visibility))
            continue;

        const auto&               item = execution.renderData.drawItems[index];
        const ShadowPushConstants push{.model = item.model, .lightIdx = lightIndex, .cascadeIndex = cascadeIndex};
        commandBuffer.pushConstants<ShadowPushConstants>(
            pipelineLayout,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0,
            push
        );
        auto&       mesh    = renderContext().resources->mesh(item.mesh);
        const auto& submesh = mesh.submeshes()[item.submeshIndex];
        mesh.bind(commandBuffer);
        commandBuffer.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        recordDraw(context().profiler, submesh.indexCount);
    }

    commandBuffer.endRendering();
}
