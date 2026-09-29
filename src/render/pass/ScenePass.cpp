#include "render/pass/ScenePass.h"

#include "asset/Asset.h"
#include "asset/AssetDescManager.h"
#include "asset/ApplyOptionalFields.h"
#include "asset/BuiltinAssets.h"
#include "core/Context.h"
#include "ecs/Light.h"
#include "ecs/Render.h"
#include "ecs/Transform.h"
#include "render/pass/LightMarkers.h"
#include "render/pass/SkyboxPass.h"
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
#include <glm/gtc/matrix_inverse.hpp>

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
                     ShaderHandle targetLightShader, std::shared_ptr<GpuTexture> brdfLut,
                     vk::Sampler targetBrdfLutSampler)
{
    vulkan      = &context;
    swapchain   = &targetSwapchain;
    frames      = &targetFrames;
    pipelines   = &targetPipelines;
    resources   = &targetResources;
    sceneShader = targetSceneShader;
    lightShader = targetLightShader;
    brdfLutTexture = std::move(brdfLut);
    brdfLutSampler = targetBrdfLutSampler;
    CHECK(brdfLutTexture != nullptr, "ScenePass requires a BRDF LUT texture");
    CHECK(brdfLutSampler, "ScenePass requires a BRDF LUT sampler");
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
    irradianceTexture.reset();
    prefilteredSpecularTexture.reset();
    radianceTexture.reset();
    brdfLutTexture.reset();
    brdfLutSampler = nullptr;
    prefilteredSpecularMaxLod = 0.0f;
    hasSkybox = false;
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

void ScenePass::bindSceneTextures(const Scene::Desc& scene)
{
    TextureBinding irradianceBinding{
        BuiltinAssets::Texture::whiteCube, BuiltinAssets::Sampler::linearClamp};
    TextureBinding prefilteredBinding = irradianceBinding;
    TextureBinding radianceBinding = irradianceBinding;
    prefilteredSpecularMaxLod = 0.0f;
    hasSkybox = scene.environment.environmentMap.has_value();
    if (scene.environment.environmentMap.has_value())
    {
        const EnvironmentMap::Desc& environment =
            context().assetDescManager->desc<EnvironmentMap>(*scene.environment.environmentMap);
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

        const Texture::Desc& radianceDesc =
            context().assetDescManager->desc<Texture>(radianceBinding.textureID);
        const Texture::Desc& irradianceDesc =
            context().assetDescManager->desc<Texture>(irradianceBinding.textureID);
        const Texture::Desc& prefilteredDesc =
            context().assetDescManager->desc<Texture>(prefilteredBinding.textureID);
        CHECK(radianceDesc.layout == ImageLayout::Cubemap &&
              irradianceDesc.layout == ImageLayout::Cubemap &&
              irradianceDesc.colorSpace == ColorSpace::Linear &&
              prefilteredDesc.layout == ImageLayout::Cubemap &&
              prefilteredDesc.colorSpace == ColorSpace::Linear &&
              prefilteredDesc.mipLevels > 0,
              "environment '{}' requires linear irradiance and prefiltered cubemaps",
              environment.id);
        prefilteredSpecularMaxLod = static_cast<float>(prefilteredDesc.mipLevels - 1);
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
    for (FrameContext& frame : *frames)
    {
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

void ScenePass::updateScene(
    uint32_t               frameIndex,
    const glm::mat4&       viewProjection,
    const glm::vec3&       cameraPosition,
    const Scene::Desc::Object* lightObject)
{
    this->cameraPosition = cameraPosition;
    inverseViewProjection = glm::inverse(viewProjection);
    SceneUniforms uniforms{
        .viewProjection = viewProjection,
        .cameraPosition = glm::vec4{cameraPosition, 1.0f},
        .iblParameters = glm::vec4{prefilteredSpecularMaxLod, 0.0f, 0.0f, 0.0f},
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
    const SkyboxPass&                   skyboxPass,
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
        GpuMesh*           mesh;
        const GpuMaterial* material;
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
            const GpuMaterial* material = submesh.material;
            if (const auto overrideIt = render->materialOverrides.find(
                    static_cast<int>(index));
                overrideIt != render->materialOverrides.end())
            {
                const Mesh::Desc& mesh = context().assetDescManager->desc<Mesh>(
                    render->meshId);
                CHECK(index < mesh.submeshes.size(),
                      "material override index out of range");
                const Material::ID materialId = mesh.submeshes[index].materialId;
                Material::Desc desc = resources->materialDesc(materialId);
                applyOptionalFields(desc, overrideIt->second);
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

    if (hasSkybox)
    {
        skyboxPass.record(commandBuffer, frames->at(frameIndex).sceneSetHandle(), {
            .inverseViewProjection = inverseViewProjection,
            .cameraPosition = glm::vec4{cameraPosition, 1.0f},
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
