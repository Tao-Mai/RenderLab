#pragma once

#include "editor/editor_ui.h"
#include "render/mesh.h"
#include "scene/light.h"
#include "window.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class AssetManager;
class Camera;
class Scene;
class Texture;
struct SceneObject;
struct Transform;

class Renderer
{
  public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer &)            = delete;
    Renderer &operator=(const Renderer &) = delete;

    void initialize(Window &window);
    void loadScene(Scene &scene, AssetManager &assets);
    void render(const Camera &camera, float deltaTime);
    [[nodiscard]] bool editorWantsInput() const;
    void waitIdle();
    void shutdown() noexcept;

  private:
    bool                             initialized = false;
    Window                          *window      = nullptr;
    vk::raii::Context                context;
    vk::raii::Instance               instance       = nullptr;
    vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
    vk::raii::SurfaceKHR             surface        = nullptr;
    vk::raii::PhysicalDevice         physicalDevice = nullptr;
    vk::raii::Device                 device         = nullptr;
    uint32_t                         queueIndex      = ~0u;
    vk::raii::Queue                  queue           = nullptr;
    vk::raii::SwapchainKHR           swapChain       = nullptr;
    std::vector<vk::Image>           swapChainImages;
    vk::SurfaceFormatKHR             swapChainSurfaceFormat;
    vk::Extent2D                     swapChainExtent;
    std::vector<vk::raii::ImageView> swapChainImageViews;
    vk::Format                       depthFormat = vk::Format::eUndefined;
    vk::raii::DeviceMemory           depthImageMemory = nullptr;
    vk::raii::Image                  depthImage = nullptr;
    vk::raii::ImageView              depthImageView = nullptr;
    vk::Format                       selectionFormat = vk::Format::eR32Uint;
    vk::raii::DeviceMemory           selectionImageMemory = nullptr;
    vk::raii::Image                  selectionImage = nullptr;
    vk::raii::ImageView              selectionImageView = nullptr;
    vk::raii::DescriptorSetLayout    materialSetLayout = nullptr;
    vk::raii::DescriptorSetLayout    sceneSetLayout = nullptr;
    vk::raii::DescriptorPool         descriptorPool = nullptr;
    vk::raii::DescriptorSet          sceneDescriptorSet = nullptr;
    vk::raii::PipelineLayout         pipelineLayout   = nullptr;
    vk::raii::Pipeline               graphicsPipeline = nullptr;
    vk::raii::Pipeline               lightPipeline = nullptr;
    vk::raii::PipelineLayout         editorPickingPipelineLayout = nullptr;
    vk::raii::Pipeline               editorPickingPipeline = nullptr;
    vk::raii::CommandPool            commandPool      = nullptr;
    vk::raii::CommandBuffer          commandBuffer    = nullptr;
    vk::raii::Semaphore              presentCompleteSemaphore = nullptr;
    vk::raii::Semaphore              renderFinishedSemaphore  = nullptr;
    vk::raii::Fence                  drawFence                = nullptr;
    std::vector<const char *>         requiredDeviceExtension{vk::KHRSwapchainExtensionName};

    struct RenderItem
    {
        Mesh *mesh = nullptr;
        SceneObject *object = nullptr;
        uint32_t selectionId = 0;
    };
    struct LightRenderItem
    {
        Light *light = nullptr;
        uint32_t selectionId = 0;
    };
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshAssets;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureAssets;
    std::shared_ptr<Texture> defaultAlbedoTexture;
    std::vector<RenderItem> renderItems;
    std::vector<LightRenderItem> lightRenderItems;
    std::unique_ptr<Mesh> lightSphereMesh;
    std::unique_ptr<Mesh> lightCubeMesh;
    std::unique_ptr<Mesh> lightArrowMesh;
    Buffer sceneUniformBuffer;
    Buffer selectionReadbackBuffer;
    Light *primaryLight = nullptr;
    SceneObject *selectedObject = nullptr;
    Light *selectedLight = nullptr;
    bool pickRequested = false;
    uint32_t pickX = 0;
    uint32_t pickY = 0;
    glm::mat4 viewProjection{1.0f};
    uint32_t swapChainMinImageCount = 0;
    EditorUI editorUI;

    void initVulkan();
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    bool isDeviceSuitable(const vk::raii::PhysicalDevice &candidate);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createDepthResources();
    void createSelectionResources();
    void createDescriptorResources();
    void createGraphicsPipeline();
    void createEditorPickingPipeline();
    void createCommandPool();
    void transitionDepthImageLayout();
    void createCommandBuffer();
    void initializeMaterials(Mesh &mesh);
    std::shared_ptr<Texture> loadTexture(const std::string &path);
    void recordCommandBuffer(uint32_t imageIndex);
    void transition_image_layout(
        uint32_t imageIndex,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::AccessFlags2 srcAccessMask,
        vk::AccessFlags2 dstAccessMask,
        vk::PipelineStageFlags2 srcStageMask,
        vk::PipelineStageFlags2 dstStageMask);
    void createSyncObjects();
    void initializeEditorUI();
    void requestSelection();
    void resolveSelection(uint32_t selectionId);
    void drawLightMarker(const Light &light);
    void drawLightMarkerForPicking(const LightRenderItem &item);
    void drawFrame();

    static uint32_t chooseSwapMinImageCount(const vk::SurfaceCapabilitiesKHR &surfaceCapabilities);
    static vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats);
    static vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes);
    vk::Format chooseDepthFormat() const;
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities);
    std::vector<const char *> getRequiredInstanceExtensions();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT *callbackData,
        void *userData);
};
