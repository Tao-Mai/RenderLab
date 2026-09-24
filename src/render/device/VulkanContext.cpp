#include "render/device/VulkanContext.h"

#include "render/device/VkCheck.h"
#include "core/Window.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>
#include "core/Logger.h"

namespace
{
const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif
}

void VulkanContext::init(Window& targetWindow)
{
    window = &targetWindow;
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
}

void VulkanContext::reset() noexcept
{
    queue                     = nullptr;
    device                    = nullptr;
    physicalDevice            = nullptr;
    surface                   = nullptr;
    debugMessenger            = nullptr;
    instance                  = nullptr;
    graphicsQueueFamilyIndex_ = ~0u;
    window                    = nullptr;
}

const vk::raii::Instance& VulkanContext::instanceHandle() const { return instance; }

const vk::raii::PhysicalDevice& VulkanContext::physicalDeviceHandle() const
{
    return physicalDevice;
}

const vk::raii::Device& VulkanContext::deviceHandle() const { return device; }
vk::raii::Queue& VulkanContext::queueHandle() { return queue; }
vk::SurfaceKHR VulkanContext::surfaceHandle() const { return *surface; }
uint32_t VulkanContext::graphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex_; }

void VulkanContext::createInstance()
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
    auto layerProperties = vkCheck(
        context.enumerateInstanceLayerProperties());
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
    CHECK(unsupportedLayerIt == requiredLayers.end(),
          "Required layer not supported: {}",
          *unsupportedLayerIt);

    // Get the required extensions.
    auto requiredExtensions = getRequiredInstanceExtensions();

    // Check if the required extensions are supported by the Vulkan implementation.
    auto extensionProperties = vkCheck(
        context.enumerateInstanceExtensionProperties());
    auto unsupportedPropertyIt =
        std::ranges::find_if(requiredExtensions,
                             [&extensionProperties](auto const& requiredExtension)
                             {
                                 return std::ranges::none_of(extensionProperties,
                                                             [requiredExtension](
                                                             auto const& extensionProperty)
                                                             {
                                                                 return strcmp(
                                                                     extensionProperty.
                                                                     extensionName,
                                                                     requiredExtension) == 0;
                                                             });
                             });
    CHECK(unsupportedPropertyIt == requiredExtensions.end(),
          "Required extension not supported: {}",
          *unsupportedPropertyIt);

    vk::InstanceCreateInfo createInfo{.pApplicationInfo = &appInfo,
                                      .enabledLayerCount = static_cast<uint32_t>(
                                          requiredLayers.size()),
                                      .ppEnabledLayerNames = requiredLayers.data(),
                                      .enabledExtensionCount = static_cast<uint32_t>(
                                          requiredExtensions.size()),
                                      .ppEnabledExtensionNames = requiredExtensions.
                                      data()};
    instance = vkCheck(context.createInstance(createInfo));
}

void VulkanContext::setupDebugMessenger()
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
    debugMessenger = vkCheck(
        instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT));
}

void VulkanContext::createSurface()
{
    surface = vk::raii::SurfaceKHR(instance, window->createVulkanSurface(*instance));
}

bool VulkanContext::isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice)
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
                                        vk::QueueFlagBits::eGraphics) &&
                                    vkCheck(
                                        physicalDevice.getSurfaceSupportKHR(
                                            qfpIndex, *surface));
                                qfpIndex++;
                                return suitable;
                            });

    // Check if all required physicalDevice extensions are available
    auto availableDeviceExtensions = vkCheck(
        physicalDevice.enumerateDeviceExtensionProperties());
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

void VulkanContext::pickPhysicalDevice()
{
    std::vector<vk::raii::PhysicalDevice> physicalDevices = vkCheck(
        instance.enumeratePhysicalDevices());
    auto const devIter = std::ranges::find_if(physicalDevices,
                                              [&](auto const& physicalDevice)
                                              {
                                                  return isDeviceSuitable(physicalDevice);
                                              });
    CHECK(devIter != physicalDevices.end(), "failed to find a suitable GPU!");
    physicalDevice = *devIter;
}

void VulkanContext::createLogicalDevice()
{
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.
        getQueueFamilyProperties();

    // get the first index into queueFamilyProperties which supports both graphics and present
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
    {
        if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
            vkCheck(
                physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface)))
        {
            // found a queue family that supports both graphics and present
            graphicsQueueFamilyIndex_ = qfpIndex;
            break;
        }
    }
    CHECK(graphicsQueueFamilyIndex_ != ~0u,
          "Could not find a queue for graphics and present -> terminating");

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
    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{.queueFamilyIndex = graphicsQueueFamilyIndex_,
                                                    .queueCount = 1,
                                                    .pQueuePriorities = &queuePriority};
    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()};

    device = vkCheck(physicalDevice.createDevice(deviceCreateInfo));
    queue  = device.getQueue(graphicsQueueFamilyIndex_, 0);
}


std::vector<const char*> VulkanContext::getRequiredInstanceExtensions()
{
    auto extensions = window->requiredVulkanExtensions();
    if (enableValidationLayers)
    {
        extensions.push_back(vk::EXTDebugUtilsExtensionName);
    }

    return extensions;
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanContext::debugCallback(
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
