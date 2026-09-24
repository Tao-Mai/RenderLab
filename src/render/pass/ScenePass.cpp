#include "render/pass/ScenePass.h"

#include "asset/AssetDesc.h"
#include "asset/AssetManager.h"
#include "core/Context.h"
#include "ecs/Light.h"
#include "ecs/Render.h"
#include "ecs/Transform.h"
#include "render/device/FrameContext.h"
#include "render/device/VkCheck.h"
#include "render/pass/LightMarkers.h"
#include "render/present/Swapchain.h"
#include "render/resource/Mesh.h"
#include "render/resource/Material.h"
#include "render/resource/RenderResourceManager.h"
#include "render/resource/ShaderData.h"
#include "render/resource/Texture.h"
#include "render/device/VulkanContext.h"
#include "ecs/Light.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>

#include <glm/vec4.hpp>

namespace
{
struct SceneUniforms
{
    glm::mat4  viewProjection{1.0f};
    glm::vec4  lightColorIntensity{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4  lightPositionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4  lightDirection{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4  lightAreaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    glm::uvec4 lightFlags{0u};
    glm::vec4  cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
};

static_assert(sizeof(SceneUniforms) == 160);
static_assert(offsetof(SceneUniforms, lightColorIntensity) == 64);
static_assert(offsetof(SceneUniforms, lightPositionRange) == 80);
static_assert(offsetof(SceneUniforms, lightDirection) == 96);
static_assert(offsetof(SceneUniforms, lightAreaSizeCone) == 112);
static_assert(offsetof(SceneUniforms, lightFlags) == 128);
static_assert(offsetof(SceneUniforms, cameraPosition) == 144);
}

void ScenePass::initDescriptors()
{
    const auto&      physicalDevice = vulkan->physicalDeviceHandle();
    const auto&      device         = vulkan->deviceHandle();
    const std::array poolSizes      = {
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1024,
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eSampler,
            .descriptorCount = 1024,
        },
        vk::DescriptorPoolSize{
            .type = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1025,
        },
    };
    const vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 1025,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    descriptorPool = vkCheck(device.createDescriptorPool(poolInfo));

    const std::array materialBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 2,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
    };
    const vk::DescriptorSetLayoutCreateInfo materialLayoutInfo{
        .bindingCount = static_cast<uint32_t>(materialBindings.size()),
        .pBindings = materialBindings.data(),
    };
    materialLayout = vkCheck(
        device.createDescriptorSetLayout(materialLayoutInfo));

    const std::array sceneBindings = {
        vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
        vk::DescriptorSetLayoutBinding{
            .binding = 2,
            .descriptorType = vk::DescriptorType::eSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        },
    };

    const vk::DescriptorSetLayoutCreateInfo sceneLayoutInfo{
        .bindingCount = sceneBindings.size(),
        .pBindings = sceneBindings.data(),
    };
    sceneLayout = vkCheck(
        device.createDescriptorSetLayout(sceneLayoutInfo));

    sceneBuffer = Buffer(
        physicalDevice,
        device,
        sizeof(SceneUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);

    const vk::DescriptorSetLayout       layout = *sceneLayout;
    const vk::DescriptorSetAllocateInfo allocationInfo{
        .descriptorPool = *descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };
    sceneSet = std::move(vkCheck(
        device.allocateDescriptorSets(allocationInfo)).front());

    const vk::DescriptorBufferInfo bufferInfo{
        .buffer = sceneBuffer.handle(),
        .range = sceneBuffer.size(),
    };
    const vk::WriteDescriptorSet write{
        .dstSet = *sceneSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &bufferInfo,
    };
    device.updateDescriptorSets(write, {});
}

void ScenePass::bindSceneDescriptor(vk::raii::CommandBuffer& commandBuffer) const
{
    const std::array sets = {*sceneSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        scenePipelines.layoutHandle(),
        sceneSetIndex,
        sets,
        {});
}

void ScenePass::init(VulkanContext& context, Swapchain&                 targetSwapchain,
                     FrameContext&  targetFrame, RenderResourceManager& targetResources)
{
    vulkan    = &context;
    swapchain = &targetSwapchain;
    frame     = &targetFrame;
    resources = &targetResources;
    initDescriptors();
    resources->configureMaterialDescriptors(*descriptorPool, *materialLayout);
    scenePipelines.init(
        vulkan->deviceHandle(),
        swapchain->surfaceFormat().format,
        swapchain->depthImageFormat(),
        *sceneLayout,
        *materialLayout,
        resources->shader("scene"),
        resources->shader("light"));
}

void ScenePass::reset() noexcept
{
    scenePipelines.reset();
    environmentTexture.reset();
    sceneSet = nullptr;
    sceneBuffer.reset();
    sceneLayout    = nullptr;
    materialLayout = nullptr;
    descriptorPool = nullptr;
    resources      = nullptr;
    frame          = nullptr;
    swapchain      = nullptr;
    vulkan         = nullptr;
}

void ScenePass::bindEnvironment(const SceneDesc& scene)
{
    const AssetId textureId = scene.environment.environmentMap.value_or(TextureDesc::whiteCube);
    environmentTexture = resources->texture(textureId);

    const vk::DescriptorImageInfo imageInfo{
        .imageView = environmentTexture->imageView(),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    const vk::DescriptorImageInfo samplerInfo{
        .sampler = environmentTexture->sampler(),
    };
    const std::array writes = {
        vk::WriteDescriptorSet{
            .dstSet = *sceneSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampledImage,
            .pImageInfo = &imageInfo,
        },
        vk::WriteDescriptorSet{
            .dstSet = *sceneSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = vk::DescriptorType::eSampler,
            .pImageInfo = &samplerInfo,
        },
    };
    vulkan->deviceHandle().updateDescriptorSets(writes, {});
}

void ScenePass::updateScene(
    const glm::mat4&       viewProjection,
    const glm::vec3&       cameraPosition,
    const SceneObjectDesc* lightObject)
{
    SceneUniforms uniforms{
        .viewProjection = viewProjection,
        .cameraPosition = glm::vec4{cameraPosition, 1.0f},
    };
    if (lightObject != nullptr)
    {
        const auto* light = lightObject->components.at("Light").try_cast<ecs::Light>();
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
    sceneBuffer.upload(&uniforms, sizeof(uniforms));
}

vk::DescriptorSetLayout ScenePass::sceneLayoutHandle() const
{
    return *sceneLayout;
}

vk::DescriptorSet ScenePass::sceneSetHandle() const
{
    return *sceneSet;
}

void ScenePass::record(
    vk::raii::CommandBuffer&            commandBuffer,
    const std::vector<SceneRenderItem>& renderItems,
    const std::vector<LightRenderItem>& lightRenderItems,
    const LightMarkers&                 lightMarkers,
    Target                              target) const
{
    vk::ClearValue              clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView = swapchain->imageView(target.imageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};
    vk::ClearValue              depthClear          = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo depthAttachmentInfo = {
        .imageView = swapchain->depthImageViewHandle(),
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
    scenePipelines.bindScene(commandBuffer);
    bindSceneDescriptor(commandBuffer);
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
    for (const SceneRenderItem& item : renderItems)
    {
        item.mesh->bind(commandBuffer);
        const ecs::Render* render =
            item.object->components.at("Render").try_cast<ecs::Render>();
        CHECK(render != nullptr, "SceneRenderItem missing Render component");

        const std::vector<Submesh>& submeshes = item.mesh->submeshes();
        for (size_t index = 0; index < submeshes.size(); ++index)
        {
            const Submesh&  submesh  = submeshes[index];
            const Material* material = submesh.material;
            if (const auto overrideIt = render->materialOverrides.find(
                    static_cast<int>(index));
                overrideIt != render->materialOverrides.end())
            {
                AssetId materialId = MaterialDesc::white;
                if (!isBuiltin<MeshDesc>(render->meshId))
                {
                    const MeshDesc& mesh = context().assetManager->desc<MeshDesc>(
                        render->meshId);
                    CHECK(index < mesh.submeshes.size(),
                          "material override index out of range");
                    materialId = mesh.submeshes[index].materialId;
                }
                MaterialDesc desc = resources->materialDesc(materialId);
                applyMaterialFields(desc, overrideIt->second);
                material = &resources->material(desc);
            }

            const MeshPushConstants pushConstants{
                .model = item.object->components.at("Transform").try_cast<
                    ecs::Transform>()->matrix(),
            };
            commandBuffer.pushConstants<MeshPushConstants>(
                scenePipelines.layoutHandle(),
                vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment,
                0,
                pushConstants);
            const std::array materialSets = {material->descriptorSetHandle()};
            commandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                scenePipelines.layoutHandle(),
                materialSetIndex,
                materialSets,
                {});
            commandBuffer.drawIndexed(
                submesh.indexCount,
                1,
                submesh.firstIndex,
                0,
                0);
        }
    }

    if (!lightRenderItems.empty())
    {
        scenePipelines.bindLightMarkers(commandBuffer);
        bindSceneDescriptor(commandBuffer);

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
                scenePipelines.layoutHandle(),
                *transform,
                *light);
        }
    }

    commandBuffer.endRendering();
}
