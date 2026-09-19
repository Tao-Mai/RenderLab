#include "render/renderer.h"
#include "asset/asset_manager.h"
#include "camera.h"
#include "render/shader.h"
#include "render/texture.h"
#include "render/vertex.h"
#include "scene/scene.h"

#include <algorithm>
#include <array>
#include <assert.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#	include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

const std::vector<char const*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace
{
struct PushConstants
{
    glm::mat4 model{1.0f};
    glm::vec4 albedo{1.0f};
};

struct SceneUniforms
{
    glm::mat4  viewProjection{1.0f};
    glm::vec4  pointLightPositionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4  pointLightColorIntensity{1.0f, 1.0f, 1.0f, 0.0f};
    glm::uvec4 pointLightFlags{0u};
};

static_assert(sizeof(SceneUniforms) == 112);
}

Renderer::~Renderer()
{
    shutdown();
}

void Renderer::initialize(Window& targetWindow)
{
    if (initialized)
    {
        return;
    }
    window = &targetWindow;
    try
    {
        initVulkan();
        initialized = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

void Renderer::render(const Camera& camera, float deltaTime)
{
    if (!initialized)
    {
        throw std::logic_error("Renderer must be initialized before render()");
    }

    const float aspect = static_cast<float>(swapChainExtent.width) /
        static_cast<float>(swapChainExtent.height);
    viewProjection = camera.projectionMatrix(aspect) * camera.viewMatrix();

    SceneUniforms sceneUniforms{.viewProjection = viewProjection};
    if (pointLight.has_value())
    {
        sceneUniforms.pointLightPositionRange = {
            pointLight->position,
            pointLight->range,
        };
        sceneUniforms.pointLightColorIntensity = {
            pointLight->color,
            pointLight->intensity,
        };
        sceneUniforms.pointLightFlags.x = pointLight->enabled ? 1u : 0u;
    }
    sceneUniformBuffer.upload(&sceneUniforms, sizeof(sceneUniforms));
    editorUI.beginFrame(deltaTime);
    drawFrame();
}

void Renderer::loadScene(const Scene& scene, AssetManager& assets)
{
    if (!initialized)
    {
        throw std::logic_error("Renderer must be initialized before loading a scene");
    }

    waitIdle();
    renderItems.clear();
    meshAssets.clear();
    lightMesh.reset();
    pointLight.reset();

    if (!scene.pointLights.empty())
    {
        pointLight = scene.pointLights.front();
    }

    lightMesh = std::make_unique<Mesh>(
        physicalDevice,
        device,
        commandPool,
        queue,
        assets.loadMesh("builtin:sphere"));

    for (const SceneObject& object : scene.objects)
    {
        auto meshIt = meshAssets.find(object.mesh);
        if (meshIt == meshAssets.end())
        {
            const MeshData& data = assets.loadMesh(object.mesh);
            auto            mesh = std::make_unique<Mesh>(
                physicalDevice,
                device,
                commandPool,
                queue,
                data);
            initializeMaterials(*mesh);
            meshIt = meshAssets.emplace(object.mesh, std::move(mesh)).first;
        }
        renderItems.push_back({meshIt->second.get(), object.transform.matrix()});
    }

}

void Renderer::waitIdle()
{
    if (*device)
    {
        device.waitIdle();
    }
}

void Renderer::shutdown() noexcept
{
    if (*device)
    {
        try
        {
            device.waitIdle();
        }
        catch (...)
        {
        }
    }

    drawFence                = nullptr;
    renderFinishedSemaphore  = nullptr;
    presentCompleteSemaphore = nullptr;
    commandBuffer            = nullptr;
    editorUI.shutdown();
    renderItems.clear();
    meshAssets.clear();
    lightMesh.reset();
    pointLight.reset();
    lightPipeline      = nullptr;
    graphicsPipeline   = nullptr;
    pipelineLayout     = nullptr;
    sceneDescriptorSet = nullptr;
    descriptorPool     = nullptr;
    sceneUniformBuffer.reset();
    textureAssets.clear();
    defaultAlbedoTexture.reset();
    sceneSetLayout    = nullptr;
    materialSetLayout = nullptr;
    commandPool       = nullptr;
    depthImageView    = nullptr;
    depthImage        = nullptr;
    depthImageMemory  = nullptr;
    depthFormat       = vk::Format::eUndefined;
    swapChainImageViews.clear();
    swapChainImages.clear();
    swapChain      = nullptr;
    queue          = nullptr;
    device         = nullptr;
    physicalDevice = nullptr;
    surface        = nullptr;
    debugMessenger = nullptr;
    instance       = nullptr;
    window         = nullptr;
    initialized    = false;
}

void Renderer::initVulkan()
{
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createCommandPool();
    createDepthResources();
    transitionDepthImageLayout();
    createDescriptorResources();
    createGraphicsPipeline();
    createCommandBuffer();
    createSyncObjects();
    initializeEditorUI();
}

void Renderer::createInstance()
{
    constexpr vk::ApplicationInfo appInfo{.pApplicationName = "Hello Triangle",
                                          .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                          .pEngineName = "No Engine",
                                          .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                          .apiVersion = vk::ApiVersion14};

    // Get the required layers
    std::vector<char const*> requiredLayers;
    if (enableValidationLayers)
    {
        requiredLayers.assign(validationLayers.begin(), validationLayers.end());
    }

    // Check if the required layers are supported by the Vulkan implementation.
    auto layerProperties    = context.enumerateInstanceLayerProperties();
    auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
                                                   [&layerProperties](
                                                   auto const& requiredLayer)
                                                   {
                                                       return std::ranges::none_of(
                                                           layerProperties,
                                                           [requiredLayer](
                                                           auto const& layerProperty)
                                                           {
                                                               return strcmp(
                                                                   layerProperty.
                                                                   layerName,
                                                                   requiredLayer) == 0;
                                                           });
                                                   });
    if (unsupportedLayerIt != requiredLayers.end())
    {
        throw std::runtime_error(
            "Required layer not supported: " + std::string(*unsupportedLayerIt));
    }

    // Get the required extensions.
    auto requiredExtensions = getRequiredInstanceExtensions();

    // Check if the required extensions are supported by the Vulkan implementation.
    auto extensionProperties   = context.enumerateInstanceExtensionProperties();
    auto unsupportedPropertyIt =
        std::ranges::find_if(requiredExtensions,
                             [&extensionProperties](auto const& requiredExtension)
                             {
                                 return std::ranges::none_of(extensionProperties,
                                     [requiredExtension](auto const& extensionProperty)
                                     {
                                         return strcmp(
                                             extensionProperty.extensionName,
                                             requiredExtension) == 0;
                                     });
                             });
    if (unsupportedPropertyIt != requiredExtensions.end())
    {
        throw std::runtime_error(
            "Required extension not supported: " + std::string(*unsupportedPropertyIt));
    }

    vk::InstanceCreateInfo createInfo{.pApplicationInfo = &appInfo,
                                      .enabledLayerCount = static_cast<uint32_t>(
                                          requiredLayers.size()),
                                      .ppEnabledLayerNames = requiredLayers.data(),
                                      .enabledExtensionCount = static_cast<uint32_t>(
                                          requiredExtensions.size()),
                                      .ppEnabledExtensionNames = requiredExtensions.
                                      data()};
    instance = vk::raii::Instance(context, createInfo);
}

void Renderer::setupDebugMessenger()
{
    if (!enableValidationLayers)
        return;

    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
        .messageSeverity = severityFlags,
        .messageType = messageTypeFlags,
        .pfnUserCallback = &debugCallback};
    debugMessenger = instance.createDebugUtilsMessengerEXT(
        debugUtilsMessengerCreateInfoEXT);
}

void Renderer::createSurface()
{
    surface = vk::raii::SurfaceKHR(instance, window->createVulkanSurface(*instance));
}

bool Renderer::isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice)
{
    // Check if the physicalDevice supports the Vulkan 1.3 API version
    bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >=
        VK_API_VERSION_1_3;

    // Check if any of the queue families support both graphics and presentation to our surface
    auto     queueFamilies              = physicalDevice.getQueueFamilyProperties();
    uint32_t qfpIndex                   = 0;
    bool     supportsGraphicsAndPresent =
        std::ranges::any_of(queueFamilies,
                            [&physicalDevice, &surface = this->surface, &qfpIndex](
                            auto const& qfp)
                            {
                                bool const suitable = (qfp.queueFlags &
                                        vk::QueueFlagBits::eGraphics) && physicalDevice.
                                    getSurfaceSupportKHR(qfpIndex, *surface);
                                qfpIndex++;
                                return suitable;
                            });

    // Check if all required physicalDevice extensions are available
    auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    bool supportsAllRequiredExtensions =
        std::ranges::all_of(requiredDeviceExtension,
                            [&availableDeviceExtensions](
                            auto const& requiredDeviceExtension)
                            {
                                return std::ranges::any_of(availableDeviceExtensions,
                                                           [requiredDeviceExtension
                                                           ](auto const&
                                                           availableDeviceExtension)
                                                           {
                                                               return strcmp(
                                                                       availableDeviceExtension
                                                                       .extensionName,
                                                                       requiredDeviceExtension)
                                                                   == 0;
                                                           });
                            });

    // Check if the physicalDevice supports the required features
    auto features = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
                                                         vk::PhysicalDeviceVulkan11Features
                                                         ,
                                                         vk::PhysicalDeviceVulkan13Features
                                                         ,
                                                         vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    bool supportsRequiredFeatures = features.template get<
            vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters &&
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2 &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().
                 extendedDynamicState;

    // Return true if the physicalDevice meets all the criteria
    return supportsVulkan1_3 && supportsGraphicsAndPresent &&
        supportsAllRequiredExtensions && supportsRequiredFeatures;
}

void Renderer::pickPhysicalDevice()
{
    std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.
        enumeratePhysicalDevices();
    auto const devIter = std::ranges::find_if(physicalDevices,
                                              [&](auto const& physicalDevice)
                                              {
                                                  return isDeviceSuitable(physicalDevice);
                                              });
    if (devIter == physicalDevices.end())
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
    physicalDevice = *devIter;
}

void Renderer::createLogicalDevice()
{
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.
        getQueueFamilyProperties();

    // get the first index into queueFamilyProperties which supports both graphics and present
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
    {
        if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
            physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
        {
            // found a queue family that supports both graphics and present
            queueIndex = qfpIndex;
            break;
        }
    }
    if (queueIndex == ~0)
    {
        throw std::runtime_error(
            "Could not find a queue for graphics and present -> terminating");
    }

    // query for Vulkan 1.3 features
    vk::StructureChain<vk::PhysicalDeviceFeatures2,
                       vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {}, // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true}, // vk::PhysicalDeviceVulkan11Features
            {.synchronization2 = true, .dynamicRendering = true},
            // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState = true}
            // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

    // create a Device
    float                     queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{.queueFamilyIndex = queueIndex,
                                                    .queueCount = 1,
                                                    .pQueuePriorities = &queuePriority};
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()};

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    queue  = vk::raii::Queue(device, queueIndex, 0);
}

void Renderer::createSwapChain()
{
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.
        getSurfaceCapabilitiesKHR(*surface);
    swapChainExtent = chooseSwapExtent(surfaceCapabilities);
    swapChainMinImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.
        getSurfaceFormatsKHR(*surface);
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.
        getSurfacePresentModesKHR(*surface);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(availablePresentModes);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{.surface = *surface,
                                                   .minImageCount = swapChainMinImageCount,
                                                   .imageFormat = swapChainSurfaceFormat.
                                                   format,
                                                   .imageColorSpace =
                                                   swapChainSurfaceFormat.colorSpace,
                                                   .imageExtent = swapChainExtent,
                                                   .imageArrayLayers = 1,
                                                   .imageUsage =
                                                   vk::ImageUsageFlagBits::eColorAttachment,
                                                   .imageSharingMode =
                                                   vk::SharingMode::eExclusive,
                                                   .preTransform = surfaceCapabilities.
                                                   currentTransform,
                                                   .compositeAlpha =
                                                   vk::CompositeAlphaFlagBitsKHR::eOpaque,
                                                   .presentMode = presentMode,
                                                   .clipped = true};

    swapChain       = vk::raii::SwapchainKHR(device, swapChainCreateInfo);
    swapChainImages = swapChain.getImages();
}

void Renderer::createImageViews()
{
    assert(swapChainImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{.viewType = vk::ImageViewType::e2D,
                                                .format = swapChainSurfaceFormat.format,
                                                .subresourceRange = {
                                                    vk::ImageAspectFlagBits::eColor, 0, 1,
                                                    0, 1}};
    for (auto& image : swapChainImages)
    {
        imageViewCreateInfo.image = image;
        swapChainImageViews.emplace_back(device, imageViewCreateInfo);
    }
}

void Renderer::createDepthResources()
{
    depthFormat = chooseDepthFormat();
    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = depthFormat,
        .extent = {swapChainExtent.width, swapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    depthImage = vk::raii::Image(device, imageInfo);

    const vk::MemoryRequirements requirements = depthImage.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    depthImageMemory = vk::raii::DeviceMemory(device, allocationInfo);
    depthImage.bindMemory(*depthImageMemory, 0);

    const vk::ImageViewCreateInfo viewInfo{
        .image = *depthImage,
        .viewType = vk::ImageViewType::e2D,
        .format = depthFormat,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    depthImageView = vk::raii::ImageView(device, viewInfo);
}

void Renderer::createDescriptorResources()
{
    const std::array bindings = {
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
    };
    const vk::DescriptorSetLayoutCreateInfo layoutInfo{
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    materialSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);

    const vk::DescriptorSetLayoutBinding sceneBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex |
        vk::ShaderStageFlagBits::eFragment,
    };
    const vk::DescriptorSetLayoutCreateInfo sceneLayoutInfo{
        .bindingCount = 1,
        .pBindings = &sceneBinding,
    };
    sceneSetLayout = vk::raii::DescriptorSetLayout(device, sceneLayoutInfo);

    const std::array poolSizes = {
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
            .descriptorCount = 1,
        },
    };
    const vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = 1025,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

    sceneUniformBuffer = Buffer(
        physicalDevice,
        device,
        sizeof(SceneUniforms),
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);

    const vk::DescriptorSetLayout       sceneLayout = *sceneSetLayout;
    const vk::DescriptorSetAllocateInfo sceneAllocationInfo{
        .descriptorPool = *descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &sceneLayout,
    };
    sceneDescriptorSet = std::move(
        vk::raii::DescriptorSets(device, sceneAllocationInfo).front());

    const vk::DescriptorBufferInfo sceneBufferInfo{
        .buffer = sceneUniformBuffer.handle(),
        .offset = 0,
        .range = sceneUniformBuffer.size(),
    };
    const vk::WriteDescriptorSet sceneWrite{
        .dstSet = *sceneDescriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &sceneBufferInfo,
    };
    device.updateDescriptorSets(sceneWrite, {});

    defaultAlbedoTexture = std::make_shared<Texture>(
        physicalDevice,
        device,
        commandPool,
        queue,
        std::array<uint8_t, 4>{255, 255, 255, 255});
}

void Renderer::createGraphicsPipeline()
{
    Shader shader(device, "shaders/slang.spv");

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex, .module = shader.handle(),
        .pName = "vertMain"};
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment, .module = shader.handle(),
        .pName = "fragMain"};
    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        vertShaderStageInfo, fragShaderStageInfo};

    const vk::VertexInputBindingDescription binding =
        Vertex::bindingDescription();
    const auto                             attributes = Vertex::attributeDescriptions();
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList
    };
    vk::PipelineViewportStateCreateInfo viewportState{.viewportCount = 1,
                                                      .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{.depthClampEnable = vk::False,
                                                        .rasterizerDiscardEnable =
                                                        vk::False,
                                                        .polygonMode =
                                                        vk::PolygonMode::eFill,
                                                        .cullMode =
                                                        vk::CullModeFlagBits::eNone,
                                                        .frontFace =
                                                        vk::FrontFace::eClockwise,
                                                        .depthBiasEnable = vk::False,
                                                        .lineWidth = 1.0f};

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False};

    vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
        | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False, .logicOp = vk::LogicOp::eCopy, .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment};

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    const vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex |
        vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(PushConstants),
    };
    const std::array descriptorSetLayouts = {
        *materialSetLayout,
        *sceneSetLayout,
    };
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()),
        .pSetLayouts = descriptorSetLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {
            {.stageCount = 2,
             .pStages = shaderStages,
             .pVertexInputState = &vertexInputInfo,
             .pInputAssemblyState = &inputAssembly,
             .pViewportState = &viewportState,
             .pRasterizationState = &rasterizer,
             .pMultisampleState = &multisampling,
             .pDepthStencilState = &depthStencil,
             .pColorBlendState = &colorBlending,
             .pDynamicState = &dynamicState,
             .layout = pipelineLayout,
             .renderPass = nullptr},
            {.colorAttachmentCount = 1,
             .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
             .depthAttachmentFormat = depthFormat}};

    graphicsPipeline = vk::raii::Pipeline(device,
                                          nullptr,
                                          pipelineCreateInfoChain.get<
                                              vk::GraphicsPipelineCreateInfo>());

    Shader lightShader(device, "shaders/light.spv");
    const std::array lightShaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = lightShader.handle(),
            .pName = "vertMain",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = lightShader.handle(),
            .pName = "fragMain",
        },
    };
    const auto lightAttributes = Vertex::positionAttributeDescription();
    const vk::PipelineVertexInputStateCreateInfo lightVertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(lightAttributes.size()),
        .pVertexAttributeDescriptions = lightAttributes.data(),
    };
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        lightPipelineCreateInfoChain = {
            {.stageCount = static_cast<uint32_t>(lightShaderStages.size()),
             .pStages = lightShaderStages.data(),
             .pVertexInputState = &lightVertexInputInfo,
             .pInputAssemblyState = &inputAssembly,
             .pViewportState = &viewportState,
             .pRasterizationState = &rasterizer,
             .pMultisampleState = &multisampling,
             .pDepthStencilState = &depthStencil,
             .pColorBlendState = &colorBlending,
             .pDynamicState = &dynamicState,
             .layout = pipelineLayout,
             .renderPass = nullptr},
            {.colorAttachmentCount = 1,
             .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
             .depthAttachmentFormat = depthFormat}};
    lightPipeline = vk::raii::Pipeline(
        device,
        nullptr,
        lightPipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
}

void Renderer::createCommandPool()
{
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queueIndex};
    commandPool = vk::raii::CommandPool(device, poolInfo);
}

void Renderer::transitionDepthImageLayout()
{
    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer transitionCommand =
        std::move(vk::raii::CommandBuffers(device, allocationInfo).front());
    transitionCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    const vk::ImageMemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
        vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = *depthImage,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    const vk::DependencyInfo dependencyInfo{
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    transitionCommand.pipelineBarrier2(dependencyInfo);
    transitionCommand.end();

    const vk::CommandBuffer command = *transitionCommand;
    const vk::SubmitInfo    submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();
}

void Renderer::createCommandBuffer()
{
    vk::CommandBufferAllocateInfo allocInfo{.commandPool = commandPool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = 1};
    commandBuffer = std::move(vk::raii::CommandBuffers(device, allocInfo).front());
}

void Renderer::recordCommandBuffer(uint32_t imageIndex)
{
    commandBuffer.reset();
    commandBuffer.begin({});

    // Before starting rendering, transition the swapchain image to vk::ImageLayout::eColorAttachmentOptimal
    transition_image_layout(
        imageIndex,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        // srcAccessMask (no need to wait for previous operations)
        vk::AccessFlagBits2::eColorAttachmentWrite,
        // dstAccessMask
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        // srcStage
        vk::PipelineStageFlagBits2::eColorAttachmentOutput // dstStage
        );
    vk::ClearValue              clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo = {
        .imageView = swapChainImageViews[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};
    vk::ClearValue              depthClear          = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo depthAttachmentInfo = {
        .imageView = *depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = depthClear,
    };
    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0}, .extent = swapChainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo};

    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);
    const std::array sceneDescriptorSets = {*sceneDescriptorSet};
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        *pipelineLayout,
        1,
        sceneDescriptorSets,
        {});
    commandBuffer.setViewport(0,
                              vk::Viewport(0.0f,
                                           0.0f,
                                           static_cast<float>(swapChainExtent.width),
                                           static_cast<float>(swapChainExtent.height),
                                           0.0f,
                                           1.0f));
    commandBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
    for (const RenderItem& item : renderItems)
    {
        item.mesh->bind(commandBuffer);
        for (const SubmeshData& submesh : item.mesh->submeshes())
        {
            const Material&     material = item.mesh->material(submesh.materialIndex);
            const PushConstants pushConstants{
                .model = item.model,
                .albedo = material.albedo,
            };
            commandBuffer.pushConstants<PushConstants>(
                *pipelineLayout,
                vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment,
                0,
                pushConstants);
            material.bind(commandBuffer, *pipelineLayout);
            commandBuffer.drawIndexed(
                submesh.indexCount,
                1,
                submesh.firstIndex,
                0,
                0);
        }
    }

    if (pointLight.has_value() && lightMesh)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *lightPipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *pipelineLayout,
            1,
            sceneDescriptorSets,
            {});

        glm::mat4 lightModel = glm::translate(glm::mat4{1.0f}, pointLight->position);
        lightModel = glm::scale(lightModel, glm::vec3{0.15f});
        const glm::vec3 markerColor = pointLight->enabled
            ? pointLight->color
            : pointLight->color * 0.15f;
        const PushConstants lightPushConstants{
            .model = lightModel,
            .albedo = glm::vec4{markerColor, 1.0f},
        };
        commandBuffer.pushConstants<PushConstants>(
            *pipelineLayout,
            vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eFragment,
            0,
            lightPushConstants);
        lightMesh->bind(commandBuffer);
        commandBuffer.drawIndexed(lightMesh->indexCount(), 1, 0, 0, 0);
    }

    commandBuffer.endRendering();

    attachmentInfo.loadOp = vk::AttachmentLoadOp::eLoad;
    renderingInfo.pDepthAttachment = nullptr;
    commandBuffer.beginRendering(renderingInfo);
    editorUI.render(*commandBuffer);
    commandBuffer.endRendering();

    // After rendering, transition the swapchain image to vk::ImageLayout::ePresentSrcKHR
    transition_image_layout(
        imageIndex,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        // srcAccessMask
        {},
        // dstAccessMask
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        // srcStage
        vk::PipelineStageFlagBits2::eBottomOfPipe // dstStage
        );
    commandBuffer.end();
}

void Renderer::initializeMaterials(Mesh& mesh)
{
    for (Material& material : mesh.materials())
    {
        std::shared_ptr<Texture> texture = material.hasAlbedoMap()
            ? loadTexture(material.albedoMap)
            : defaultAlbedoTexture;
        material.createDescriptorSet(
            device,
            *descriptorPool,
            *materialSetLayout,
            std::move(texture));
    }
}

std::shared_ptr<Texture> Renderer::loadTexture(const std::string& path)
{
    if (path.empty() || path.starts_with("data:") || path.starts_with("embedded:"))
    {
        return defaultAlbedoTexture;
    }
    if (const auto cached = textureAssets.find(path); cached != textureAssets.end())
    {
        return cached->second;
    }

    auto texture = std::make_shared<Texture>(
        physicalDevice,
        device,
        commandPool,
        queue,
        std::filesystem::path(path));
    textureAssets.emplace(path, texture);
    return texture;
}

void Renderer::transition_image_layout(
    uint32_t                imageIndex,
    vk::ImageLayout         old_layout,
    vk::ImageLayout         new_layout,
    vk::AccessFlags2        src_access_mask,
    vk::AccessFlags2        dst_access_mask,
    vk::PipelineStageFlags2 src_stage_mask,
    vk::PipelineStageFlags2 dst_stage_mask)
{
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = src_stage_mask,
        .srcAccessMask = src_access_mask,
        .dstStageMask = dst_stage_mask,
        .dstAccessMask = dst_access_mask,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapChainImages[imageIndex],
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1}};
    vk::DependencyInfo dependency_info = {
        .dependencyFlags = {},
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier};
    commandBuffer.pipelineBarrier2(dependency_info);
}

void Renderer::createSyncObjects()
{
    presentCompleteSemaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo());
    renderFinishedSemaphore = vk::raii::Semaphore(device, vk::SemaphoreCreateInfo());
    drawFence = vk::raii::Fence(device, {.flags = vk::FenceCreateFlagBits::eSignaled});
}

void Renderer::initializeEditorUI()
{
    editorUI.initialize(
        window->nativeHandle(),
        *instance,
        *physicalDevice,
        *device,
        queueIndex,
        *queue,
        static_cast<VkFormat>(swapChainSurfaceFormat.format),
        swapChainMinImageCount,
        static_cast<uint32_t>(swapChainImages.size()));
}

void Renderer::drawFrame()
{
    auto fenceResult = device.waitForFences(*drawFence, vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess)
    {
        throw std::runtime_error("failed to wait for fence!");
    }
    device.resetFences(*drawFence);

    auto [result, imageIndex] = swapChain.acquireNextImage(
        UINT64_MAX,
        *presentCompleteSemaphore,
        nullptr);

    recordCommandBuffer(imageIndex);

    queue.waitIdle();
    // NOTE: for simplicity, wait for the queue to be idle before starting the frame
    // In the next chapter you see how to use multiple frames in flight and fences to sync

    vk::PipelineStageFlags waitDestinationStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submitInfo{.waitSemaphoreCount = 1,
                                    .pWaitSemaphores = &*presentCompleteSemaphore,
                                    .pWaitDstStageMask = &waitDestinationStageMask,
                                    .commandBufferCount = 1,
                                    .pCommandBuffers = &*commandBuffer,
                                    .signalSemaphoreCount = 1,
                                    .pSignalSemaphores = &*renderFinishedSemaphore};
    queue.submit(submitInfo, *drawFence);

    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &*swapChain,
        .pImageIndices = &imageIndex
    };
    result = queue.presentKHR(presentInfoKHR);
    switch (result)
    {
        case vk::Result::eSuccess:
            break;
        case vk::Result::eSuboptimalKHR:
            std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n";
            break;
        default:
            break; // an unexpected result is returned!
    }
}

uint32_t Renderer::chooseSwapMinImageCount(
    vk::SurfaceCapabilitiesKHR const& surfaceCapabilities)
{
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) && (surfaceCapabilities.maxImageCount <
        minImageCount))
    {
        minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
}

vk::SurfaceFormatKHR Renderer::chooseSwapSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
    assert(!availableFormats.empty());
    const auto formatIt = std::ranges::find_if(
        availableFormats,
        [](const auto& format)
        {
            return format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace ==
                vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
}

vk::PresentModeKHR Renderer::chooseSwapPresentMode(
    std::vector<vk::PresentModeKHR> const& availablePresentModes)
{
    assert(
        std::ranges::any_of(availablePresentModes, [](auto presentMode) { return
            presentMode == vk::PresentModeKHR::eFifo; }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value)
                               {
                                   return vk::PresentModeKHR::eMailbox == value;
                               })
        ? vk::PresentModeKHR::eMailbox
        : vk::PresentModeKHR::eFifo;
}

vk::Format Renderer::chooseDepthFormat() const
{
    constexpr std::array candidates = {
        vk::Format::eD32Sfloat,
        vk::Format::eD32SfloatS8Uint,
        vk::Format::eD24UnormS8Uint,
    };
    for (const vk::Format format : candidates)
    {
        const vk::FormatProperties properties = physicalDevice.
            getFormatProperties(format);
        if ((properties.optimalTilingFeatures &
                vk::FormatFeatureFlagBits::eDepthStencilAttachment) !=
            vk::FormatFeatureFlags
            {})
        {
            return format;
        }
    }
    throw std::runtime_error("failed to find a supported depth format");
}

uint32_t Renderer::findMemoryType(
    uint32_t                typeFilter,
    vk::MemoryPropertyFlags properties) const
{
    const vk::PhysicalDeviceMemoryProperties memoryProperties =
        physicalDevice.getMemoryProperties();
    for (uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index)
    {
        if ((typeFilter & (1u << index)) != 0 &&
            (memoryProperties.memoryTypes[index].propertyFlags & properties) ==
            properties)
        {
            return index;
        }
    }
    throw std::runtime_error("failed to find a suitable Vulkan memory type");
}

vk::Extent2D Renderer::chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    const auto [width, height] = window->framebufferSize();

    return {
        std::clamp<uint32_t>(width,
                             capabilities.minImageExtent.width,
                             capabilities.maxImageExtent.width),
        std::clamp<uint32_t>(height,
                             capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height)};
}

std::vector<const char*> Renderer::getRequiredInstanceExtensions()
{
    auto extensions = window->requiredVulkanExtensions();
    if (enableValidationLayers)
    {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL Renderer::debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT      severity,
    vk::DebugUtilsMessageTypeFlagsEXT             type,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
{
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError || severity ==
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
    {
        std::cerr << "validation layer: type " << to_string(type) << " msg: " <<
            pCallbackData->pMessage << std::endl;
    }

    return vk::False;
}
