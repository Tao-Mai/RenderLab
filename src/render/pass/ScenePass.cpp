#include "render/pass/ScenePass.h"

#include "asset/AssetDesc.h"
#include "asset/AssetDescManager.h"
#include "core/Context.h"
#include "ecs/Light.h"
#include "ecs/Render.h"
#include "ecs/Transform.h"
#include "render/pass/LightMarkers.h"
#include "render/present/Swapchain.h"
#include "render/resource/Mesh.h"
#include "render/resource/Material.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"
#include "render/resource/Texture.h"
#include "render/device/VulkanContext.h"

#include <algorithm>
#include <array>
#include <utility>

#include <glm/vec4.hpp>

void ScenePass::bindSceneDescriptor(
    vk::raii::CommandBuffer& commandBuffer, uint32_t frameIndex) const
{
    const std::array sets = {frames->at(frameIndex).sceneSetHandle()};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipelineLayout,
        RenderInterface::sceneSet,
        sets,
        {});
}

void ScenePass::init(VulkanContext& context, Swapchain& targetSwapchain,
                     std::array<FrameContext, maxFramesInFlight>& targetFrames,
                     PipelineManager& targetPipelines,
                     RenderResourceManager& targetResources, ShaderHandle targetSceneShader,
                     ShaderHandle targetLightShader)
{
    vulkan      = &context;
    swapchain   = &targetSwapchain;
    frames      = &targetFrames;
    pipelines   = &targetPipelines;
    resources   = &targetResources;
    sceneShader = targetSceneShader;
    lightShader = targetLightShader;
    refreshPipelines();
}

void ScenePass::refreshPipelines()
{
    const vk::Format colorFormat = swapchain->surfaceFormat().format;
    const vk::Format depthFormat = swapchain->depthImageFormat();
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
}

void ScenePass::reset() noexcept
{
    environmentTexture.reset();
    scenePipeline       = nullptr;
    lightMarkerPipeline = nullptr;
    pipelineLayout      = nullptr;
    sceneShader         = {};
    lightShader         = {};
    resources           = nullptr;
    pipelines           = nullptr;
    frames              = nullptr;
    swapchain           = nullptr;
    vulkan              = nullptr;
}

void ScenePass::bindEnvironment(const SceneDesc& scene)
{
    AssetId textureId = TextureDesc::whiteCube;
    if (scene.environment.environmentMap.has_value())
    {
        const EnvironmentMapDesc& environment =
            context().assetManager->desc<EnvironmentMapDesc>(*scene.environment.environmentMap);
        CHECK(!environment.radiance.empty(),
              "environment '{}' has no radiance texture",
              environment.id);
        textureId = environment.radiance;
    }
    environmentTexture      = resources->texture(textureId);
    const vk::DescriptorImageInfo imageInfo{
        .imageView = environmentTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo samplerInfo{.sampler = environmentTexture->sampler()};
    for (FrameContext& frame : *frames)
    {
        const std::array writes = {
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::environmentImageBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .pImageInfo = &imageInfo,
            },
            vk::WriteDescriptorSet{
                .dstSet = frame.sceneSetHandle(),
                .dstBinding = RenderInterface::environmentSamplerBinding,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eSampler,
                .pImageInfo = &samplerInfo,
            },
        };
        vulkan->deviceHandle().updateDescriptorSets(writes, {});
    }
}

void ScenePass::updateScene(
    uint32_t               frameIndex,
    const glm::mat4&       viewProjection,
    const glm::vec3&       cameraPosition,
    const SceneObjectDesc* lightObject)
{
    this->cameraPosition = cameraPosition;
    SceneUniforms uniforms{
        .viewProjection = viewProjection,
        .cameraPosition = glm::vec4{cameraPosition, 1.0f},
    };
    if (lightObject != nullptr)
    {
        const auto* light     = lightObject->components.at("Light").try_cast<ecs::Light>();
        const auto* transform =
            lightObject->components.at("Transform").try_cast<ecs::Transform>();
        CHECK(light != nullptr, "light object missing Light component");
        CHECK(transform != nullptr, "light object missing Transform component");

        const glm::vec3 direction =
            glm::normalize(transform->rotation * glm::vec3{0.0f, 0.0f, -1.0f});
        uniforms.lightColorIntensity = {light->color, light->intensity};
        uniforms.lightPositionRange  = {transform->position, light->range};
        uniforms.lightDirection      = glm::vec4{direction, 0.0f};
        uniforms.lightAreaSizeCone   = {
            light->areaSize, light->cosInner, light->cosOuter,
        };
        uniforms.lightFlags = {
            static_cast<uint32_t>(light->type),
            light->enabled ? 1u : 0u,
            light->castShadow ? 1u : 0u,
            0u,
        };
    }
    frames->at(frameIndex).updateScene(uniforms);
}

vk::DescriptorSet ScenePass::sceneSetHandle(uint32_t frameIndex) const
{
    return frames->at(frameIndex).sceneSetHandle();
}

void ScenePass::record(
    vk::raii::CommandBuffer&            commandBuffer,
    const std::vector<SceneRenderItem>& renderItems,
    const std::vector<LightRenderItem>& lightRenderItems,
    const LightMarkers&                 lightMarkers,
    uint32_t                            frameIndex,
    Target                              target) const
{
    vk::ClearValue              clearColor     = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView = swapchain->imageView(target.imageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};
    vk::ClearValue              depthClear          = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo depthAttachmentInfo = {
        .imageView = swapchain->depthImageViewHandle(frameIndex),
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = target.storeDepthForPicking
        ? vk::AttachmentStoreOp::eStore
        : vk::AttachmentStoreOp::eDontCare,
        .clearValue = depthClear,
    };
    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0}, .extent = swapchain->extent()},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo};

    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, scenePipeline);
    bindSceneDescriptor(commandBuffer, frameIndex);
    const ViewportRect editorViewport = target.viewport;
    const uint32_t     viewportX      = std::min(
        static_cast<uint32_t>(std::max(editorViewport.x, 0.0f)),
        swapchain->extent().width - 1);
    const uint32_t viewportY = std::min(
        static_cast<uint32_t>(std::max(editorViewport.y, 0.0f)),
        swapchain->extent().height - 1);
    const uint32_t viewportWidth = std::max(
        1u,
        std::min(
            static_cast<uint32_t>(editorViewport.width),
            swapchain->extent().width - viewportX));
    const uint32_t viewportHeight = std::max(
        1u,
        std::min(
            static_cast<uint32_t>(editorViewport.height),
            swapchain->extent().height - viewportY));
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
    struct DrawCall
    {
        Mesh*           mesh;
        const Material* material;
        glm::mat4       model;
        uint32_t        firstIndex;
        uint32_t        indexCount;
        float           distanceSquared;
    };
    std::vector<DrawCall> opaqueDraws;
    std::vector<DrawCall> transparentDraws;
    for (const SceneRenderItem& item : renderItems)
    {
        const auto* render    = item.object->components.at("Render").try_cast<ecs::Render>();
        const auto* transform =
            item.object->components.at("Transform").try_cast<ecs::Transform>();
        CHECK(render != nullptr && transform != nullptr,
              "SceneRenderItem is missing Render or Transform");
        const glm::vec3             offset          = transform->position - cameraPosition;
        const float                 distanceSquared = glm::dot(offset, offset);
        const std::vector<Submesh>& submeshes       = item.mesh->submeshes();
        for (size_t index = 0; index < submeshes.size(); ++index)
        {
            const Submesh&  submesh  = submeshes[index];
            const Material* material = submesh.material;
            if (const auto overrideIt = render->materialOverrides.find(
                    static_cast<int>(index));
                overrideIt != render->materialOverrides.end())
            {
                const MeshDesc& mesh = context().assetManager->desc<MeshDesc>(
                    render->meshId);
                CHECK(index < mesh.submeshes.size(),
                      "material override index out of range");
                const AssetId materialId = mesh.submeshes[index].materialId;
                MaterialDesc desc = resources->materialDesc(materialId);
                applyMaterialFields(desc, overrideIt->second);
                material = &resources->material(desc);
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

    vk::Pipeline boundPipeline = scenePipeline;
    const auto   draw          = [&](const DrawCall& item)
    {
        const vk::Pipeline pipeline = pipelines->getOrCreate(
            item.material->pipelineKey(
                swapchain->surfaceFormat().format,
                swapchain->depthImageFormat()));
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
            const auto* light = item.object->components.at("Light").try_cast<
                ecs::Light>();
            const auto* transform =
                item.object->components.at("Transform").try_cast<ecs::Transform>();
            CHECK(light != nullptr, "light item missing Light component");
            CHECK(transform != nullptr, "light item missing Transform component");
            lightMarkers.record(
                commandBuffer,
                pipelineLayout,
                *transform,
                *light);
        }
    }

    commandBuffer.endRendering();
}
