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
#include <cmath>
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
#include <glm/gtc/quaternion.hpp>

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
struct MeshPushConstants
{
    glm::mat4 model{1.0f};
};

struct LightPushConstants
{
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

struct EditorPickingPushConstants
{
    glm::mat4 model{1.0f};
    uint32_t selectionId = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

struct SceneUniforms
{
    glm::mat4 viewProjection{1.0f};
    glm::vec4 lightColorIntensity{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec4 lightPositionRange{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 lightDirection{0.0f, -1.0f, 0.0f, 0.0f};
    glm::vec4 lightAreaSizeCone{1.0f, 1.0f, 0.9396926f, 0.8660254f};
    glm::uvec4 lightFlags{0u};
    glm::vec4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
};

glm::quat lightRotation(const glm::vec3 &direction)
{
    const glm::vec3 forward = glm::normalize(direction);
    const glm::vec3 up = std::abs(glm::dot(forward, glm::vec3{0.0f, 1.0f, 0.0f})) >
            0.999f
        ? glm::vec3{0.0f, 0.0f, 1.0f}
        : glm::vec3{0.0f, 1.0f, 0.0f};
    return glm::normalize(glm::quatLookAtRH(forward, up));
}

glm::mat4 lightTransform(const Light &light)
{
    return glm::translate(glm::mat4{1.0f}, light.position) *
        glm::mat4_cast(lightRotation(light.direction));
}

static_assert(sizeof(SceneUniforms) == 160);
static_assert(sizeof(MeshPushConstants) == 64);
static_assert(sizeof(LightPushConstants) == 80);
static_assert(sizeof(EditorPickingPushConstants) == 80);
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

    editorUI.beginFrame(deltaTime);
    const glm::mat4 view = camera.viewMatrix();
    const glm::mat4 projection = camera.projectionMatrix(editorUI.sceneAspectRatio());

    if (selectedObject != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        editorUI.drawGizmo(
            selectedObject->transform,
            view,
            gizmoProjection,
            !camera.isNavigationActive());
    }
    else if (selectedLight != nullptr)
    {
        glm::mat4 gizmoProjection = projection;
        gizmoProjection[1][1] *= -1.0f;
        editorUI.drawLightGizmo(
            *selectedLight,
            view,
            gizmoProjection,
            !camera.isNavigationActive());
    }
    if (!camera.isNavigationActive())
    {
        requestSelection();
    }
    editorUI.drawInspector(selectedObject, selectedLight);
    editorUI.endFrame();

    viewProjection = projection * view;

    SceneUniforms sceneUniforms{
        .viewProjection = viewProjection,
        .cameraPosition = glm::vec4{camera.worldPosition(), 1.0f},
    };
    if (primaryLight != nullptr)
    {
        sceneUniforms.lightColorIntensity = {
            primaryLight->color,
            primaryLight->intensity,
        };
        sceneUniforms.lightPositionRange = {
            primaryLight->position,
            primaryLight->range,
        };
        sceneUniforms.lightDirection = glm::vec4{primaryLight->direction, 0.0f};
        sceneUniforms.lightAreaSizeCone = {
            primaryLight->areaSize,
            primaryLight->cosInner,
            primaryLight->cosOuter,
        };
        sceneUniforms.lightFlags = {
            static_cast<uint32_t>(primaryLight->type),
            primaryLight->enabled ? 1u : 0u,
            primaryLight->castShadow ? 1u : 0u,
            0u,
        };
    }
    sceneUniformBuffer.upload(&sceneUniforms, sizeof(sceneUniforms));
    drawFrame();
}

bool Renderer::editorWantsInput() const
{
    return editorUI.wantsInput();
}

void Renderer::loadScene(Scene& scene, AssetManager& assets)
{
    if (!initialized)
    {
        throw std::logic_error("Renderer must be initialized before loading a scene");
    }

    waitIdle();
    renderItems.clear();
    lightRenderItems.clear();
    meshAssets.clear();
    lightSphereMesh.reset();
    lightCubeMesh.reset();
    lightArrowMesh.reset();
    primaryLight = nullptr;
    selectedObject = nullptr;
    selectedLight = nullptr;
    pickRequested = false;

    if (!scene.lights.empty())
    {
        primaryLight = &scene.lights.front();
        lightSphereMesh = std::make_unique<Mesh>(
            physicalDevice,
            device,
            commandPool,
            queue,
            assets.loadMesh("builtin:sphere"));
        lightCubeMesh = std::make_unique<Mesh>(
            physicalDevice,
            device,
            commandPool,
            queue,
            assets.loadMesh("builtin:cube"));
        lightArrowMesh = std::make_unique<Mesh>(
            physicalDevice,
            device,
            commandPool,
            queue,
            assets.loadMesh("builtin:arrow"));
    }

    uint32_t nextSelectionId = 1;
    for (SceneObject& object : scene.objects)
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
        renderItems.push_back({meshIt->second.get(), &object, nextSelectionId++});
    }

    for (Light &light : scene.lights)
    {
        lightRenderItems.push_back({&light, nextSelectionId++});
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
    lightRenderItems.clear();
    meshAssets.clear();
    lightSphereMesh.reset();
    lightCubeMesh.reset();
    lightArrowMesh.reset();
    primaryLight = nullptr;
    selectedObject = nullptr;
    selectedLight = nullptr;
    pickRequested = false;
    editorPickingPipeline = nullptr;
    editorPickingPipelineLayout = nullptr;
    lightPipeline      = nullptr;
    graphicsPipeline   = nullptr;
    pipelineLayout     = nullptr;
    sceneDescriptorSet = nullptr;
    descriptorPool     = nullptr;
    sceneUniformBuffer.reset();
    selectionReadbackBuffer.reset();
    textureAssets.clear();
    defaultAlbedoTexture.reset();
    sceneSetLayout    = nullptr;
    materialSetLayout = nullptr;
    commandPool       = nullptr;
    depthImageView    = nullptr;
    depthImage        = nullptr;
    depthImageMemory  = nullptr;
    depthFormat       = vk::Format::eUndefined;
    selectionImageView   = nullptr;
    selectionImage       = nullptr;
    selectionImageMemory = nullptr;
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
    createSelectionResources();
    transitionDepthImageLayout();
    createDescriptorResources();
    createGraphicsPipeline();
    createEditorPickingPipeline();
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

void Renderer::createSelectionResources()
{
    const vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = selectionFormat,
        .extent = {swapChainExtent.width, swapChainExtent.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment |
                 vk::ImageUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    selectionImage = vk::raii::Image(device, imageInfo);

    const vk::MemoryRequirements requirements = selectionImage.getMemoryRequirements();
    const vk::MemoryAllocateInfo allocationInfo{
        .allocationSize = requirements.size,
        .memoryTypeIndex = findMemoryType(
            requirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    selectionImageMemory = vk::raii::DeviceMemory(device, allocationInfo);
    selectionImage.bindMemory(*selectionImageMemory, 0);

    const vk::ImageViewCreateInfo viewInfo{
        .image = *selectionImage,
        .viewType = vk::ImageViewType::e2D,
        .format = selectionFormat,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    selectionImageView = vk::raii::ImageView(device, viewInfo);

    selectionReadbackBuffer = Buffer(
        physicalDevice,
        device,
        sizeof(uint32_t),
        vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
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
        vk::DescriptorSetLayoutBinding{
            .binding = 2,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
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
            .descriptorCount = 1025,
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

    const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR |
                          vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB |
                          vk::ColorComponentFlagBits::eA,
    };

    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    const vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex |
        vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(LightPushConstants),
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

void Renderer::createEditorPickingPipeline()
{
    Shader shader(device, "shaders/editor_picking.spv");
    const std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shader.handle(),
            .pName = "vertMain",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shader.handle(),
            .pName = "fragMain",
        },
    };

    const vk::VertexInputBindingDescription binding = Vertex::bindingDescription();
    const auto attributes = Vertex::positionAttributeDescription();
    const vk::PipelineVertexInputStateCreateInfo vertexInputInfo{
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };
    const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList,
    };
    const vk::PipelineViewportStateCreateInfo viewportState{
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
    };
    const vk::PipelineDepthStencilStateCreateInfo depthStencil{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::False,
        .depthCompareOp = vk::CompareOp::eLessOrEqual,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
    };
    const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR,
    };
    const vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
    };
    constexpr std::array dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const vk::PushConstantRange pushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eVertex |
                      vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(EditorPickingPushConstants),
    };
    const vk::DescriptorSetLayout sceneLayout = *sceneSetLayout;
    const vk::PipelineLayoutCreateInfo layoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &sceneLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange,
    };
    editorPickingPipelineLayout = vk::raii::PipelineLayout(device, layoutInfo);

    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {
            {
                .stageCount = static_cast<uint32_t>(shaderStages.size()),
                .pStages = shaderStages.data(),
                .pVertexInputState = &vertexInputInfo,
                .pInputAssemblyState = &inputAssembly,
                .pViewportState = &viewportState,
                .pRasterizationState = &rasterizer,
                .pMultisampleState = &multisampling,
                .pDepthStencilState = &depthStencil,
                .pColorBlendState = &colorBlending,
                .pDynamicState = &dynamicState,
                .layout = *editorPickingPipelineLayout,
                .renderPass = nullptr,
            },
            {
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats = &selectionFormat,
                .depthAttachmentFormat = depthFormat,
            },
        };
    editorPickingPipeline = vk::raii::Pipeline(
        device,
        nullptr,
        pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
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

void Renderer::drawLightMarker(const Light &light)
{
    const glm::vec3 markerColor = light.enabled
        ? light.color
        : light.color * 0.15f;
    const auto drawMesh = [this](
                              Mesh &mesh,
                              const glm::mat4 &model,
                              const glm::vec3 &color,
                              uint32_t firstIndex,
                              uint32_t indexCount)
    {
        const LightPushConstants pushConstants{
            .model = model,
            .color = glm::vec4{color, 1.0f},
        };
        commandBuffer.pushConstants<LightPushConstants>(
            *pipelineLayout,
            vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants);
        mesh.bind(commandBuffer);
        commandBuffer.drawIndexed(indexCount, 1, firstIndex, 0, 0);
    };

    switch (light.type)
    {
    case Light::Type::Point:
    case Light::Type::Spot:
        if (lightSphereMesh)
        {
            const glm::mat4 model = glm::translate(
                glm::mat4{1.0f}, light.position) *
                glm::scale(glm::mat4{1.0f}, glm::vec3{0.15f});
            drawMesh(
                *lightSphereMesh,
                model,
                markerColor,
                0,
                lightSphereMesh->indexCount());
        }
        break;

    case Light::Type::RectArea:
        if (lightCubeMesh)
        {
            constexpr float thickness = 0.08f;
            constexpr uint32_t indicesPerFace = 6;
            constexpr uint32_t emittingFace = 1;
            const glm::mat4 model = lightTransform(light) *
                glm::scale(
                    glm::mat4{1.0f},
                    glm::vec3{light.areaSize, thickness});
            const glm::vec3 housingColor = light.enabled
                ? glm::vec3{0.28f}
                : glm::vec3{0.12f};
            for (uint32_t face = 0; face < 6; ++face)
            {
                drawMesh(
                    *lightCubeMesh,
                    model,
                    face == emittingFace ? markerColor : housingColor,
                    face * indicesPerFace,
                    indicesPerFace);
            }
        }
        break;

    case Light::Type::Directional:
        if (lightArrowMesh)
        {
            const glm::mat4 base = lightTransform(light);
            constexpr float spacing = 0.32f;
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    const glm::mat4 model = base *
                        glm::translate(
                            glm::mat4{1.0f},
                            glm::vec3{x * spacing, y * spacing, 0.0f}) *
                        glm::scale(glm::mat4{1.0f}, glm::vec3{0.75f});
                    drawMesh(
                        *lightArrowMesh,
                        model,
                        markerColor,
                        0,
                        lightArrowMesh->indexCount());
                }
            }
        }
        break;
    }
}

void Renderer::drawLightMarkerForPicking(const LightRenderItem &item)
{
    const auto drawMesh = [this, &item](Mesh &mesh, const glm::mat4 &model)
    {
        const EditorPickingPushConstants pushConstants{
            .model = model,
            .selectionId = item.selectionId,
        };
        commandBuffer.pushConstants<EditorPickingPushConstants>(
            *editorPickingPipelineLayout,
            vk::ShaderStageFlagBits::eVertex |
            vk::ShaderStageFlagBits::eFragment,
            0,
            pushConstants);
        mesh.bind(commandBuffer);
        commandBuffer.drawIndexed(mesh.indexCount(), 1, 0, 0, 0);
    };

    const Light &light = *item.light;
    switch (light.type)
    {
    case Light::Type::Point:
    case Light::Type::Spot:
        if (lightSphereMesh)
        {
            drawMesh(
                *lightSphereMesh,
                glm::translate(glm::mat4{1.0f}, light.position) *
                    glm::scale(glm::mat4{1.0f}, glm::vec3{0.15f}));
        }
        break;

    case Light::Type::RectArea:
        if (lightCubeMesh)
        {
            drawMesh(
                *lightCubeMesh,
                lightTransform(light) *
                    glm::scale(
                        glm::mat4{1.0f},
                        glm::vec3{light.areaSize, 0.08f}));
        }
        break;

    case Light::Type::Directional:
        if (lightArrowMesh)
        {
            const glm::mat4 base = lightTransform(light);
            constexpr float spacing = 0.32f;
            for (int y = -1; y <= 1; ++y)
            {
                for (int x = -1; x <= 1; ++x)
                {
                    drawMesh(
                        *lightArrowMesh,
                        base *
                            glm::translate(
                                glm::mat4{1.0f},
                                glm::vec3{x * spacing, y * spacing, 0.0f}) *
                            glm::scale(glm::mat4{1.0f}, glm::vec3{0.75f}));
                }
            }
        }
        break;
    }
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
        .storeOp = pickRequested
            ? vk::AttachmentStoreOp::eStore
            : vk::AttachmentStoreOp::eDontCare,
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
    const EditorUI::ViewportRect editorViewport = editorUI.sceneViewportPixels();
    const uint32_t viewportX = std::min(
        static_cast<uint32_t>(std::max(editorViewport.x, 0.0f)),
        swapChainExtent.width - 1);
    const uint32_t viewportY = std::min(
        static_cast<uint32_t>(std::max(editorViewport.y, 0.0f)),
        swapChainExtent.height - 1);
    const uint32_t viewportWidth = std::max(
        1u,
        std::min(
            static_cast<uint32_t>(editorViewport.width),
            swapChainExtent.width - viewportX));
    const uint32_t viewportHeight = std::max(
        1u,
        std::min(
            static_cast<uint32_t>(editorViewport.height),
            swapChainExtent.height - viewportY));
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
    for (const RenderItem& item : renderItems)
    {
        item.mesh->bind(commandBuffer);
        for (const SubmeshData& submesh : item.mesh->submeshes())
        {
            const Material& material = item.mesh->material(submesh.materialIndex);
            const MeshPushConstants pushConstants{
                .model = item.object->transform.matrix(),
            };
            commandBuffer.pushConstants<MeshPushConstants>(
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

    if (!lightRenderItems.empty())
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *lightPipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *pipelineLayout,
            1,
            sceneDescriptorSets,
            {});

        for (const LightRenderItem &item : lightRenderItems)
        {
            drawLightMarker(*item.light);
        }
    }

    commandBuffer.endRendering();

    if (pickRequested)
    {
        const std::array pickingPassBarriers = {
            vk::ImageMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                .srcAccessMask = {},
                .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = *selectionImage,
                .subresourceRange = {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
            },
            vk::ImageMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                vk::PipelineStageFlagBits2::eLateFragmentTests,
                .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
                .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead,
                .oldLayout = vk::ImageLayout::eDepthAttachmentOptimal,
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
            },
        };
        const vk::DependencyInfo pickingPassDependency{
            .imageMemoryBarrierCount = static_cast<uint32_t>(pickingPassBarriers.size()),
            .pImageMemoryBarriers = pickingPassBarriers.data(),
        };
        commandBuffer.pipelineBarrier2(pickingPassDependency);

        const vk::ClearValue selectionClear = vk::ClearColorValue(
            std::array<uint32_t, 4>{0u, 0u, 0u, 0u});
        const vk::RenderingAttachmentInfo selectionAttachment{
            .imageView = *selectionImageView,
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = selectionClear,
        };
        const vk::RenderingAttachmentInfo pickingDepthAttachment{
            .imageView = *depthImageView,
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
        };
        const vk::RenderingInfo pickingRenderingInfo{
            .renderArea = {
                .offset = {
                    static_cast<int32_t>(pickX),
                    static_cast<int32_t>(pickY),
                },
                .extent = {1, 1},
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &selectionAttachment,
            .pDepthAttachment = &pickingDepthAttachment,
        };
        commandBuffer.beginRendering(pickingRenderingInfo);
        commandBuffer.bindPipeline(
            vk::PipelineBindPoint::eGraphics,
            *editorPickingPipeline);
        commandBuffer.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            *editorPickingPipelineLayout,
            0,
            sceneDescriptorSets,
            {});
        commandBuffer.setScissor(
            0,
            vk::Rect2D(
                vk::Offset2D(
                    static_cast<int32_t>(pickX),
                    static_cast<int32_t>(pickY)),
                vk::Extent2D(1, 1)));

        for (const RenderItem &item : renderItems)
        {
            item.mesh->bind(commandBuffer);
            const EditorPickingPushConstants pushConstants{
                .model = item.object->transform.matrix(),
                .selectionId = item.selectionId,
            };
            commandBuffer.pushConstants<EditorPickingPushConstants>(
                *editorPickingPipelineLayout,
                vk::ShaderStageFlagBits::eVertex |
                vk::ShaderStageFlagBits::eFragment,
                0,
                pushConstants);
            for (const SubmeshData &submesh : item.mesh->submeshes())
            {
                commandBuffer.drawIndexed(
                    submesh.indexCount,
                    1,
                    submesh.firstIndex,
                    0,
                    0);
            }
        }

        for (const LightRenderItem &item : lightRenderItems)
        {
            drawLightMarkerForPicking(item);
        }
        commandBuffer.endRendering();

        const vk::ImageMemoryBarrier2 selectionReadBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = *selectionImage,
            .subresourceRange = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        const vk::DependencyInfo selectionReadDependency{
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &selectionReadBarrier,
        };
        commandBuffer.pipelineBarrier2(selectionReadDependency);

        const vk::BufferImageCopy copyRegion{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = {
                static_cast<int32_t>(pickX),
                static_cast<int32_t>(pickY),
                0,
            },
            .imageExtent = {1, 1, 1},
        };
        commandBuffer.copyImageToBuffer(
            *selectionImage,
            vk::ImageLayout::eTransferSrcOptimal,
            selectionReadbackBuffer.handle(),
            copyRegion);
    }

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
            physicalDevice,
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

void Renderer::requestSelection()
{
    glm::vec2 clickNdc;
    if (!editorUI.sceneClicked(clickNdc))
    {
        return;
    }

    const EditorUI::ViewportRect viewport = editorUI.sceneViewportPixels();
    const float normalizedX = std::clamp(clickNdc.x * 0.5f + 0.5f, 0.0f, 1.0f);
    const float normalizedY = std::clamp(clickNdc.y * 0.5f + 0.5f, 0.0f, 1.0f);
    pickX = std::min(
        static_cast<uint32_t>(std::max(viewport.x + normalizedX * viewport.width, 0.0f)),
        swapChainExtent.width - 1);
    pickY = std::min(
        static_cast<uint32_t>(std::max(viewport.y + normalizedY * viewport.height, 0.0f)),
        swapChainExtent.height - 1);
    pickRequested = true;
}

void Renderer::resolveSelection(uint32_t selectionId)
{
    selectedObject = nullptr;
    selectedLight = nullptr;

    if (selectionId == 0)
    {
        return;
    }
    for (const RenderItem &item : renderItems)
    {
        if (item.selectionId == selectionId)
        {
            selectedObject = item.object;
            return;
        }
    }
    for (const LightRenderItem &item : lightRenderItems)
    {
        if (item.selectionId == selectionId)
        {
            selectedLight = item.light;
            return;
        }
    }
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

    const bool resolvePickAfterSubmit = pickRequested;
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

    if (resolvePickAfterSubmit)
    {
        const vk::Result pickFenceResult = device.waitForFences(
            *drawFence,
            vk::True,
            UINT64_MAX);
        if (pickFenceResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("failed to wait for object picking readback");
        }

        uint32_t selectionId = 0;
        selectionReadbackBuffer.download(&selectionId, sizeof(selectionId));
        resolveSelection(selectionId);
        pickRequested = false;
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
