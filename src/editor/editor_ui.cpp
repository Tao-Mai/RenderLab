#include "editor/editor_ui.h"

#include <stdexcept>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>

EditorUI::~EditorUI()
{
    shutdown();
}

void EditorUI::initialize(
    GLFWwindow *window,
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    uint32_t queueFamily,
    VkQueue queue,
    VkFormat targetColorFormat,
    uint32_t minImageCount,
    uint32_t imageCount)
{
    if (contextCreated)
    {
        return;
    }

    try
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        contextCreated = true;

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForVulkan(window, true))
        {
            throw std::runtime_error("failed to initialize ImGui GLFW backend");
        }
        glfwBackendInitialized = true;

        colorFormat = targetColorFormat;
        pipelineRenderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &colorFormat,
        };

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_4;
        initInfo.Instance = instance;
        initInfo.PhysicalDevice = physicalDevice;
        initInfo.Device = device;
        initInfo.QueueFamily = queueFamily;
        initInfo.Queue = queue;
        initInfo.DescriptorPoolSize = 64;
        initInfo.MinImageCount = minImageCount;
        initInfo.ImageCount = imageCount;
        initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineRenderingCreateInfo = pipelineRenderingInfo;

        if (!ImGui_ImplVulkan_Init(&initInfo))
        {
            throw std::runtime_error("failed to initialize ImGui Vulkan backend");
        }
        vulkanBackendInitialized = true;
    }
    catch (...)
    {
        shutdown();
        throw;
    }
}

void EditorUI::beginFrame(float deltaTime)
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    fpsRefreshTime += deltaTime;
    ++framesSinceRefresh;
    if (fpsRefreshTime >= 0.5f)
    {
        displayedFps = static_cast<float>(framesSinceRefresh) / fpsRefreshTime;
        fpsRefreshTime = 0.0f;
        framesSinceRefresh = 0;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode |
                                         ImGuiDockNodeFlags_NoDockingOverCentralNode;
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, viewport, dockFlags);

    ImGuiID editorDockId = 0;
    if (!dockLayoutInitialized)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(
            dockspaceId,
            ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID centerDockId = dockspaceId;
        editorDockId = ImGui::DockBuilderSplitNode(
            centerDockId,
            ImGuiDir_Right,
            0.24f,
            nullptr,
            &centerDockId);
        ImGui::DockBuilderDockWindow("Editor", editorDockId);
        ImGui::DockBuilderFinish(dockspaceId);
        dockLayoutInitialized = true;
    }

    if (editorDockId != 0)
    {
        ImGui::SetNextWindowDockID(editorDockId, ImGuiCond_Always);
    }

    constexpr ImGuiWindowFlags editorFlags = ImGuiWindowFlags_NoMove |
                                              ImGuiWindowFlags_NoCollapse |
                                              ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Editor", nullptr, editorFlags);
    ImGui::TextUnformatted("Performance");
    ImGui::Separator();
    ImGui::Text("FPS: %.1f", displayedFps);
    ImGui::End();

    ImGui::Render();
}

void EditorUI::render(VkCommandBuffer commandBuffer) const
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void EditorUI::shutdown() noexcept
{
    if (vulkanBackendInitialized)
    {
        ImGui_ImplVulkan_Shutdown();
        vulkanBackendInitialized = false;
    }
    if (glfwBackendInitialized)
    {
        ImGui_ImplGlfw_Shutdown();
        glfwBackendInitialized = false;
    }
    if (contextCreated)
    {
        ImGui::DestroyContext();
        contextCreated = false;
    }
    dockLayoutInitialized = false;
}
