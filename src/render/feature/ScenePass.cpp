#include "render/feature/ScenePass.h"

#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/feature/RenderGraph.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuTexture.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <array>
#include <utility>

#include <glm/vec4.hpp>

void ScenePass::bindSceneDescriptor(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex) const
{
    const std::array sets = {graph->frameData(frameIndex).sceneSetHandle()};
    commandBuffer
        .bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, RenderInterface::kSceneSet, sets, {});
}

ScenePass::Output ScenePass::setup(
    RenderGraph&                    targetGraph,
    RenderGraph::ImageHandle        colorHandle,
    PointLightShadowPass::Output    pointShadow,
    RectAreaLightShadowPass::Output rectAreaShadow
)
{
    graph  = &targetGraph;
    vulkan = &graph->vulkan();
    CHECK(
        vulkan->physicalDeviceHandle().getFormatProperties(vk::Format::eD32Sfloat).optimalTilingFeatures &
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );

    pipelines         = renderContext().pipelineManager;
    resources         = renderContext().resources;
    color             = graph->write(colorHandle);
    pointShadowMap    = pointShadow.pointLightShadowMap;
    rectAreaShadowMap = rectAreaShadow.rectAreaLightShadowMap;

    const auto& brdf = context().config->rendererConfig().brdfLut;
    CHECK(!brdf.textureID.empty() && !brdf.samplerID.empty());
    const auto& brdfAsset = context().assetManager->get<TextureAsset>(brdf.textureID);
    CHECK(
        brdfAsset.layout == ImageLayout::Image2D && brdfAsset.colorSpace == ColorSpace::Linear &&
            brdfAsset.format != ImageFormat::R8,
        "BRDF LUT must be a linear 2D texture with at least two channels"
    );
    brdfLutTexture = resources->texture(brdf.textureID);
    brdfLutSampler = resources->sampler(brdf.samplerID);
    bindEnvironmentTextures();

    const vk::Filter filter = context().config->rendererConfig().feature.shadow.filter ==
            SamplerAsset::Filter::Linear
        ? vk::Filter::eLinear
        : vk::Filter::eNearest;
    if (!*shadowSampler || shadowFilter != filter)
    {
        shadowSampler = vkCheck(vulkan->deviceHandle().createSampler(
            {.magFilter     = filter,
             .minFilter     = filter,
             .mipmapMode    = vk::SamplerMipmapMode::eNearest,
             .addressModeU  = vk::SamplerAddressMode::eClampToEdge,
             .addressModeV  = vk::SamplerAddressMode::eClampToEdge,
             .addressModeW  = vk::SamplerAddressMode::eClampToEdge,
             .compareEnable = true,
             .compareOp     = vk::CompareOp::eLessOrEqual,
             .maxLod        = 0.0f}
        ));
        shadowFilter = filter;
    }

    auto&      graph  = targetGraph;
    const auto extent = graph.swapchain().extent();
    depth = graph.createImage({.format = vk::Format::eD32Sfloat, .extent = {extent.width, extent.height, 1}});
    depth = graph.write(depth);
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
            .access =
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
            .layout = vk::ImageLayout::eDepthAttachmentOptimal,
        }
    );
    graph.use(
        pointShadowMap,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
        }
    );
    // Keep the generated map in the graph and leave it ready for future sampling.
    graph.use(
        rectAreaShadowMap,
        {
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
        }
    );

    const vk::Format colorFormat = graph.swapchain().surfaceFormat().format;
    iblEnabled                   = context().config->rendererConfig().feature.ibl;
    sceneShader =
        renderContext().shaderManager->getOrLoad("scene", iblEnabled ? ShaderVariant::Default : ShaderVariant::NoIbl);
    scenePipeline  = pipelines->getOrCreate({
         .shader      = sceneShader,
         .layout      = PipelineLayoutPreset::SceneMaterial,
         .state       = PipelineState::preset(RenderMode::Opaque),
         .colorFormat = colorFormat,
         .depthFormat = vk::Format::eD32Sfloat,
    });
    pipelineLayout = pipelines->layout(PipelineLayoutPreset::SceneMaterial);

    if (hasSkybox)
    {
        skyboxShader = renderContext().shaderManager->getOrLoad("skybox");
        skyboxPipeline = pipelines->getOrCreate({
            .shader      = skyboxShader,
            .layout      = PipelineLayoutPreset::SceneOnly,
            .state       = PipelineState::preset(RenderMode::Skybox),
            .colorFormat = colorFormat,
            .depthFormat = vk::Format::eD32Sfloat,
        });
        skyboxPipelineLayout = pipelines->layout(PipelineLayoutPreset::SceneOnly);
    }
    return {color, depth};
}

void ScenePass::bindResources(RenderGraph& graph) const
{
    const vk::DescriptorImageInfo linearSamplerInfo{
        .sampler = resources->sampler(BuiltinAssets::Sampler::linearClamp),
    };

    for (uint32_t frameIndex = 0; frameIndex < maxFramesInFlight; ++frameIndex)
    {
        const vk::DescriptorImageInfo shadowImage{
            .imageView = graph.getImage(pointShadowMap, frameIndex)
                             .getOrCreateImageView({
                                 .viewType = vk::ImageViewType::eCubeArray,
                                 .format   = vk::Format::eD32Sfloat,
                                 .subresourceRange =
                                     {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                      .levelCount = 1,
                                      .layerCount = kMaxPointLightShadow * 6},
                             }),
            .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo rectAreaShadowImage{
            .imageView = graph.getImage(rectAreaShadowMap, frameIndex)
                             .getOrCreateImageView({
                                 .viewType = vk::ImageViewType::e2DArray,
                                 .format   = vk::Format::eD32Sfloat,
                                 .subresourceRange =
                                     {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                      .levelCount = 1,
                                      .layerCount = kMaxRectAreaLightShadow},
                             }),
            .imageLayout = vk::ImageLayout::eDepthReadOnlyOptimal,
        };
        const vk::DescriptorImageInfo shadowSamplerInfo{.sampler = *shadowSampler};
        const std::array              writes = {
            vk::WriteDescriptorSet{
                             .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                             .dstBinding      = RenderInterface::kPointShadowImageBinding,
                             .descriptorCount = 1,
                             .descriptorType  = vk::DescriptorType::eSampledImage,
                             .pImageInfo      = &shadowImage,
            },
            vk::WriteDescriptorSet{
                             .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                             .dstBinding      = RenderInterface::kRectAreaShadowImageBinding,
                             .descriptorCount = 1,
                             .descriptorType  = vk::DescriptorType::eSampledImage,
                             .pImageInfo      = &rectAreaShadowImage
            },
            vk::WriteDescriptorSet{
                             .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                             .dstBinding      = RenderInterface::kShadowComparisonSamplerBinding,
                             .descriptorCount = 1,
                             .descriptorType  = vk::DescriptorType::eSampler,
                             .pImageInfo      = &shadowSamplerInfo
            },
            vk::WriteDescriptorSet{
                             .dstSet          = graph.frameData(frameIndex).sceneSetHandle(),
                             .dstBinding      = RenderInterface::kLinearSamplerBinding,
                             .descriptorCount = 1,
                             .descriptorType  = vk::DescriptorType::eSampler,
                             .pImageInfo      = &linearSamplerInfo,
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void ScenePass::bindEnvironmentTextures()
{
    TextureBinding irradianceBinding{BuiltinAssets::Texture::whiteCube, BuiltinAssets::Sampler::linearClamp};
    TextureBinding prefilteredBinding = irradianceBinding;
    TextureBinding radianceBinding    = irradianceBinding;
    const auto&    environmentMap     = context().config->rendererConfig().environmentMap;
    hasSkybox = context().config->rendererConfig().feature.ibl && environmentMap.has_value();
    if (environmentMap)
    {
        const auto& environment = context().assetManager->get<EnvironmentMapAsset>(*environmentMap);
        CHECK(
            !environment.radiance.textureID.empty() && !environment.radiance.samplerID.empty() &&
                !environment.irradiance.textureID.empty() && !environment.irradiance.samplerID.empty() &&
                !environment.prefilteredSpecular.textureID.empty() &&
                !environment.prefilteredSpecular.samplerID.empty(),
            "environment '{}' requires radiance, irradiance and prefiltered specular textures",
            environment.id
        );
        radianceBinding    = environment.radiance;
        irradianceBinding  = environment.irradiance;
        prefilteredBinding = environment.prefilteredSpecular;

        const TextureAsset& radianceDesc    = context().assetManager->get<TextureAsset>(radianceBinding.textureID);
        const TextureAsset& irradianceDesc  = context().assetManager->get<TextureAsset>(irradianceBinding.textureID);
        const TextureAsset& prefilteredDesc = context().assetManager->get<TextureAsset>(prefilteredBinding.textureID);
        CHECK(
            radianceDesc.layout == ImageLayout::Cubemap && irradianceDesc.layout == ImageLayout::Cubemap &&
                irradianceDesc.colorSpace == ColorSpace::Linear && prefilteredDesc.layout == ImageLayout::Cubemap &&
                prefilteredDesc.colorSpace == ColorSpace::Linear && prefilteredDesc.mipLevels > 0,
            "environment '{}' requires linear irradiance and prefiltered cubemaps",
            environment.id
        );
    }

    irradianceTexture          = resources->texture(irradianceBinding.textureID);
    prefilteredSpecularTexture = resources->texture(prefilteredBinding.textureID);
    radianceTexture            = resources->texture(radianceBinding.textureID);
    const vk::DescriptorImageInfo irradianceImage{
        .imageView   = irradianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo irradianceSampler{.sampler = resources->sampler(irradianceBinding.samplerID)};
    const vk::DescriptorImageInfo prefilteredImage{
        .imageView   = prefilteredSpecularTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo prefilteredSampler{.sampler = resources->sampler(prefilteredBinding.samplerID)};
    const vk::DescriptorImageInfo lutImage{
        .imageView   = brdfLutTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo lutSampler{.sampler = brdfLutSampler};
    const vk::DescriptorImageInfo radianceImage{
        .imageView   = radianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo radianceSampler{.sampler = resources->sampler(radianceBinding.samplerID)};
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
    {
        FrameData&       frame  = graph->frameData(index);
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kIrradianceImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &irradianceImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kIrradianceSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &irradianceSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kPrefilteredSpecularImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &prefilteredImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kPrefilteredSpecularSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &prefilteredSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kBrdfLutImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &lutImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kBrdfLutSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &lutSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kRadianceImageBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampledImage,
                .pImageInfo      = &radianceImage,
            },
            vk::WriteDescriptorSet{
                .dstSet          = frame.sceneSetHandle(),
                .dstBinding      = RenderInterface::kRadianceSamplerBinding,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eSampler,
                .pImageInfo      = &radianceSampler,
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void ScenePass::recordSkybox(
    vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex, const SkyboxPushConstants& parameters
) const
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, skyboxPipeline);

    const std::array sets = {graph->frameData(frameIndex).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        skyboxPipelineLayout,
        RenderInterface::kSceneSet,
        sets,
        {}
    );
    commandBuffer.pushConstants<SkyboxPushConstants>(
        skyboxPipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        parameters
    );
    commandBuffer.draw(3, 1, 0, 0);
}

void ScenePass::execute(RenderGraph& graph) const
{
    auto&             commandBuffer = graph.commands();
    const RenderData& renderData    = graph.renderData();
    const uint32_t    frameIndex    = graph.frameIndex();
    const auto        extent        = graph.swapchain().extent();


    vk::ClearValue              clearColor     = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView   = graph.getImage(color).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = graph.swapchain().surfaceFormat().format,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = clearColor
    };
    vk::ClearValue              depthClear          = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo depthAttachmentInfo = {
        .imageView   = graph.getImage(depth).getOrCreateImageView({
              .viewType         = vk::ImageViewType::e2D,
              .format           = vk::Format::eD32Sfloat,
              .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eDepth, .levelCount = 1, .layerCount = 1},
        }),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp      = vk::AttachmentLoadOp::eClear,
        .storeOp     = vk::AttachmentStoreOp::eStore,
        .clearValue  = depthClear,
    };
    vk::RenderingInfo renderingInfo = {
        .renderArea           = {.offset = {0, 0}, .extent = extent},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &attachmentInfo,
        .pDepthAttachment     = &depthAttachmentInfo
    };

    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, scenePipeline);
    bindSceneDescriptor(commandBuffer, frameIndex);
    const ViewportRect editorViewport = graph.editorInput().viewport;
    const uint32_t     viewportX = std::min(static_cast<uint32_t>(std::max(editorViewport.x, 0.0f)), extent.width - 1);
    const uint32_t     viewportY = std::min(static_cast<uint32_t>(std::max(editorViewport.y, 0.0f)), extent.height - 1);
    const uint32_t     viewportWidth =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.width), extent.width - viewportX));
    const uint32_t viewportHeight =
        std::max(1u, std::min(static_cast<uint32_t>(editorViewport.height), extent.height - viewportY));
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
    commandBuffer.setScissor(
        0,
        vk::Rect2D(
            vk::Offset2D(static_cast<int32_t>(viewportX), static_cast<int32_t>(viewportY)),
            vk::Extent2D(viewportWidth, viewportHeight)
        )
    );
    vk::Pipeline boundPipeline = scenePipeline;
    const auto   draw          = [&](const DrawItem& item)
    {
        PipelineKey key = item.material->pipelineKey(graph.swapchain().surfaceFormat().format, vk::Format::eD32Sfloat);
        if (!iblEnabled)
        {
            const ShaderAsset::ID shaderId = renderContext().shaderManager->metadata(key.shader).id;
            key.shader                     = renderContext().shaderManager->getOrLoad(shaderId, ShaderVariant::NoIbl);
        }
        const vk::Pipeline pipeline = pipelines->getOrCreate(key);
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
    };
    for (const DrawItem& item : renderData.opaqueDrawItems)
    {
        draw(item);
    }

    if (hasSkybox)
    {
        recordSkybox(
            commandBuffer,
            frameIndex,
            {
                .viewport = glm::vec4{
                    static_cast<float>(viewportX),
                    static_cast<float>(viewportY),
                    static_cast<float>(viewportWidth),
                    static_cast<float>(viewportHeight)
                },
            }
        );
        boundPipeline = nullptr;
    }

    for (const DrawItem& item : renderData.transparentDrawItems)
    {
        draw(item);
    }

    commandBuffer.endRendering();
}
