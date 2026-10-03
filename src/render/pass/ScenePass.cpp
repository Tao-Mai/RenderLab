#include "render/pass/ScenePass.h"

#include "render/pass/RenderGraph.h"
#include "render/pass/ShadowPass.h"
#include "render/Renderer.h"

#include "asset/Asset.h"
#include "asset/AssetManager.h"
#include "asset/ApplyOptionalFields.h"
#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "scene/component/LightComponent.h"
#include "scene/component/RenderComponent.h"
#include "scene/component/TransformComponent.h"
#include "render/pass/LightMarkers.h"
#include "render/present/Swapchain.h"
#include "render/resource/GpuMesh.h"
#include "render/resource/GpuMaterial.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"
#include "render/resource/GpuTexture.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
#include <array>
#include <utility>

#include <glm/vec4.hpp>

void ScenePass::bindSceneDescriptor(
    vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex) const
{
    const std::array sets = {graph->frameContext(frameIndex).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::sceneSet,
        sets,
        {});
}

void ScenePass::init(RenderGraph& targetGraph,
    std::shared_ptr<GpuTexture> brdfLut, vk::Sampler targetBrdfLutSampler)
{
    graph = &targetGraph;
    vulkan = &graph->vulkan();
    pipelines = &graph->pipelines();
    resources = &graph->assetResources();
    sceneShader = graph->shaders().getOrLoad("scene");
    lightShader = graph->shaders().getOrLoad("light");
    skyboxShader = graph->shaders().getOrLoad("skybox");

    brdfLutTexture = std::move(brdfLut);
    brdfLutSampler = targetBrdfLutSampler;
    DCHECK(brdfLutTexture && brdfLutSampler, "ScenePass requires a BRDF LUT binding");
}

void ScenePass::registerPass(RenderGraph& graph)
{
    std::tie(inputSlots, outputSlots) = graph.registerPass("scene", InputCount, OutputCount,
        [this](RenderGraph& g) { setupPass(g); },
        [this](RenderGraph& g) { executePass(g); });
}

void ScenePass::setupPass(RenderGraph& graph)
{
    const RenderGraph::ResourceDesc colorUse{
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .access = vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };
    const auto extent = graph.swapchain().extent();
    const RenderGraph::ResourceDesc depthUse{
        .format = graph.depthFormat(),
        .extent = {extent.width, extent.height, 1},
        .aspect = vk::ImageAspectFlagBits::eDepth,
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .access = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .layout = vk::ImageLayout::eDepthAttachmentOptimal,
    };
    graph.importSwapchain(inputSlots[Color], colorUse);
    graph.createResource(inputSlots[Depth], depthUse);
    graph.bindOutput(outputSlots[ColorResult], inputSlots[Color], colorUse);
    graph.bindOutput(outputSlots[DepthResult], inputSlots[Depth], depthUse);

    if (graph.passEnabled("shadow"))
        graph.bindInput(inputSlots[Shadow], graph.outputSlot("shadow", ShadowPass::ShadowMap), {
            .imageUsage = vk::ImageUsageFlagBits::eSampled,
            .stages = vk::PipelineStageFlagBits2::eFragmentShader,
            .access = vk::AccessFlagBits2::eShaderSampledRead,
            .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
        });
    else
        graph.ignoreInput(inputSlots[Shadow]);

    graph.importTexture(inputSlots[Irradiance], *irradianceTexture);
    graph.importTexture(inputSlots[Prefiltered], *prefilteredSpecularTexture);
    graph.importTexture(inputSlots[Radiance], *radianceTexture);
    graph.importTexture(inputSlots[BrdfLut], *brdfLutTexture);

    const vk::Format colorFormat = graph.swapchain().surfaceFormat().format;
    const vk::Format depthFormat = graph.depthFormat();
    scenePipeline                = pipelines->getOrCreate({
        .shader = sceneShader,
        .layout = PipelineLayoutPreset::SceneMaterial,
        .state = PipelineState::preset(RenderMode::Opaque),
        .colorFormat = colorFormat,
        .depthFormat = depthFormat,
    });
    lightMarkerPipeline = pipelines->getOrCreate({
        .shader = lightShader,
        .layout = PipelineLayoutPreset::SceneMaterial,
        .state = PipelineState::preset(RenderMode::LightMarker),
        .colorFormat = colorFormat,
        .depthFormat = depthFormat,
    });
    pipelineLayout = pipelines->layout(PipelineLayoutPreset::SceneMaterial);

    skyboxPipeline = pipelines->getOrCreate({
        .shader = skyboxShader,
        .layout = PipelineLayoutPreset::SceneOnly,
        .state = PipelineState::preset(RenderMode::Skybox),
        .colorFormat = colorFormat,
        .depthFormat = depthFormat,
    });
    skyboxPipelineLayout = pipelines->layout(PipelineLayoutPreset::SceneOnly);
}

void ScenePass::bindSceneTextures(const SceneAsset& scene)
{
    TextureBinding irradianceBinding{
        BuiltinAssets::Texture::whiteCube, BuiltinAssets::Sampler::linearClamp};
    TextureBinding prefilteredBinding = irradianceBinding;
    TextureBinding radianceBinding = irradianceBinding;
    hasSkybox = scene.environment.environmentMap.has_value();
    if (scene.environment.environmentMap.has_value())
    {
        const EnvironmentMapAsset& environment =
            context().assetManager->get<EnvironmentMapAsset>(*scene.environment.environmentMap);
        CHECK(!environment.radiance.textureID.empty() &&
              !environment.radiance.samplerID.empty() &&
              !environment.irradiance.textureID.empty() &&
              !environment.irradiance.samplerID.empty() &&
              !environment.prefilteredSpecular.textureID.empty() &&
              !environment.prefilteredSpecular.samplerID.empty(),
              "environment '{}' requires radiance, irradiance and prefiltered specular textures",
              environment.id);
        radianceBinding = environment.radiance;
        irradianceBinding = environment.irradiance;
        prefilteredBinding = environment.prefilteredSpecular;

        const TextureAsset& radianceDesc =
            context().assetManager->get<TextureAsset>(radianceBinding.textureID);
        const TextureAsset& irradianceDesc =
            context().assetManager->get<TextureAsset>(irradianceBinding.textureID);
        const TextureAsset& prefilteredDesc =
            context().assetManager->get<TextureAsset>(prefilteredBinding.textureID);
        CHECK(radianceDesc.layout == ImageLayout::Cubemap &&
              irradianceDesc.layout == ImageLayout::Cubemap &&
              irradianceDesc.colorSpace == ColorSpace::Linear &&
              prefilteredDesc.layout == ImageLayout::Cubemap &&
              prefilteredDesc.colorSpace == ColorSpace::Linear &&
              prefilteredDesc.mipLevels > 0,
              "environment '{}' requires linear irradiance and prefiltered cubemaps",
              environment.id);
    }

    irradianceTexture = resources->texture(irradianceBinding.textureID);
    prefilteredSpecularTexture = resources->texture(prefilteredBinding.textureID);
    radianceTexture = resources->texture(radianceBinding.textureID);
    const vk::DescriptorImageInfo irradianceImage{
        .imageView = irradianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo irradianceSampler{
        .sampler = resources->sampler(irradianceBinding.samplerID)};
    const vk::DescriptorImageInfo prefilteredImage{
        .imageView = prefilteredSpecularTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo prefilteredSampler{
        .sampler = resources->sampler(prefilteredBinding.samplerID)};
    const vk::DescriptorImageInfo lutImage{
        .imageView = brdfLutTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo lutSampler{.sampler = brdfLutSampler};
    const vk::DescriptorImageInfo radianceImage{
        .imageView = radianceTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo radianceSampler{
        .sampler = resources->sampler(radianceBinding.samplerID)};
    for (uint32_t index = 0; index < maxFramesInFlight; ++index)
    {
        FrameContext& frame = graph->frameContext(index);
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::irradianceImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &irradianceImage,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::irradianceSamplerBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &irradianceSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::prefilteredSpecularImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &prefilteredImage,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::prefilteredSpecularSamplerBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &prefilteredSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::brdfLutImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &lutImage,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::brdfLutSamplerBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &lutSampler,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::radianceImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &radianceImage,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::radianceSamplerBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &radianceSampler,
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void ScenePass::prepareRenderData(const FrameContext& frame)
{
    const glm::vec3 cameraPosition = frame.viewUniforms().cameraPosition;
    opaqueDraws.clear();
    transparentDraws.clear();
    const auto& renderItems = graph->scene().renderItems();
    for (const SceneRenderItem& item : renderItems)
    {
        const auto* render = item.actor->getComponent<RenderComponent>();
        const auto* transform = item.actor->getComponent<TransformComponent>();
        DCHECK(render && transform,
              "SceneRenderItem is missing Render or Transform");
        const glm::vec3             offset          = transform->position - cameraPosition;
        const float                 distanceSquared = glm::dot(offset, offset);
        const std::vector<Submesh>& submeshes       = item.mesh->submeshes();
        for (size_t index = 0; index < submeshes.size(); ++index)
        {
            const Submesh&  submesh  = submeshes[index];
            const GpuMaterial* material = submesh.material;
            if (const auto overrideIt = render->materialOverrides.find(
                    static_cast<int>(index));
                overrideIt != render->materialOverrides.end())
            {
                const MeshAsset& mesh = context().assetManager->get<MeshAsset>(
                    render->meshId);
                CHECK(index < mesh.submeshes.size(),
                      "material override index out of range");
                const MaterialAsset::ID materialId = mesh.submeshes[index].materialId;
                MaterialAsset asset = resources->materialAsset(materialId);
                applyOptionalFields(asset, overrideIt->second);
                material = &resources->material(asset);
            }
            DrawCall draw{
                item.mesh, material, transform->matrix(),
                submesh.firstIndex, submesh.indexCount, distanceSquared,
            };
            (material->renderMode() == RenderMode::Transparent
                ? transparentDraws
                : opaqueDraws).push_back(draw);
        }
    }
    std::sort(transparentDraws.begin(),
              transparentDraws.end(),
              [](const DrawCall& left, const DrawCall& right)
              {
                  return left.distanceSquared > right.distanceSquared;
              });
}

void ScenePass::recordSkybox(vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex,
    const SkyboxPushConstants& parameters) const
{
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, skyboxPipeline);

    const std::array sets = {graph->frameContext(frameIndex).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
        skyboxPipelineLayout, RenderInterface::sceneSet, sets, {});
    commandBuffer.pushConstants<SkyboxPushConstants>(skyboxPipelineLayout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0, parameters);
    commandBuffer.draw(3, 1, 0, 0);
}

void ScenePass::executePass(RenderGraph& graph) const
{
    auto& commandBuffer = graph.commands();
    const auto& lightRenderItems = graph.scene().lightRenderItems();
    const auto& lightMarkers = graph.scene().lightMarkers();
    const uint32_t frameIndex = graph.frameIndex();
    const auto extent = graph.swapchain().extent();

    vk::ClearValue              clearColor     = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView = graph.imageView(inputSlots[Color]),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};
    vk::ClearValue              depthClear          = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo depthAttachmentInfo = {
        .imageView = graph.imageView(inputSlots[Depth]),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = depthClear,
    };
    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0}, .extent = extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo};

    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, scenePipeline);
    bindSceneDescriptor(commandBuffer, frameIndex);
    const ViewportRect editorViewport = graph.editorInput().viewport;
    const uint32_t     viewportX      = std::min(
        static_cast<uint32_t>(std::max(editorViewport.x, 0.0f)),
        extent.width - 1);
    const uint32_t viewportY = std::min(
        static_cast<uint32_t>(std::max(editorViewport.y, 0.0f)),
        extent.height - 1);
    const uint32_t viewportWidth = std::max(
        1u,
        std::min(
            static_cast<uint32_t>(editorViewport.width),
            extent.width - viewportX));
    const uint32_t viewportHeight = std::max(
        1u,
        std::min(
            static_cast<uint32_t>(editorViewport.height),
            extent.height - viewportY));
    commandBuffer.setViewport(
        0,
        vk::Viewport(
            static_cast<float>(viewportX),
            static_cast<float>(viewportY),
            static_cast<float>(viewportWidth),
            static_cast<float>(viewportHeight),
            0.0f,
            1.0f));
    commandBuffer.setScissor(
        0,
        vk::Rect2D(
            vk::Offset2D(
                static_cast<int32_t>(viewportX),
                static_cast<int32_t>(viewportY)),
            vk::Extent2D(viewportWidth, viewportHeight)));
    vk::Pipeline boundPipeline = scenePipeline;
    const auto   draw          = [&](const DrawCall& item)
    {
        const vk::Pipeline pipeline = pipelines->getOrCreate(
            item.material->pipelineKey(
                graph.swapchain().surfaceFormat().format,
                graph.depthFormat()));
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
            pushConstants);
        const std::array materialSets = {item.material->descriptorSetHandle()};
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipelineLayout,
            RenderInterface::materialSet,
            materialSets,
            {});
        commandBuffer.drawIndexed(item.indexCount, 1, item.firstIndex, 0, 0);
    };
    for (const DrawCall& item : opaqueDraws)
    {
        draw(item);
    }

    if (hasSkybox)
    {
        recordSkybox(commandBuffer, frameIndex, {
            .viewport = glm::vec4{
                static_cast<float>(viewportX), static_cast<float>(viewportY),
                static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)},
        });
        boundPipeline = nullptr;
    }

    for (const DrawCall& item : transparentDraws)
    {
        draw(item);
    }

    if (!lightRenderItems.empty())
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, lightMarkerPipeline);
        bindSceneDescriptor(commandBuffer, frameIndex);

        for (const LightRenderItem& item : lightRenderItems)
        {
            const auto* light = item.actor->getComponent<LightComponent>();
            const auto* transform = item.actor->getComponent<TransformComponent>();
            DCHECK(light);
            DCHECK(transform);
            lightMarkers.record(
                commandBuffer,
                pipelineLayout,
                *transform,
                *light);
        }
    }

    commandBuffer.endRendering();
}
