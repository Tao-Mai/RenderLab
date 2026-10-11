#include "editor/EditorBackend.h"
#include <array>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <string>
#include "core/Logger.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

EditorBackend::~EditorBackend() { DCHECK(!contextCreated && !glfwBackendInitialized && !vulkanBackendInitialized); }

void EditorBackend::init(
    GLFWwindow*      window,
    VkInstance       instance,
    VkPhysicalDevice physicalDevice,
    VkDevice         device,
    uint32_t         queueFamily,
    VkQueue          queue,
    VkFormat         targetColorFormat,
    uint32_t         minImageCount,
    uint32_t         imageCount
)
{
    DCHECK(!contextCreated && !glfwBackendInitialized && !vulkanBackendInitialized);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

#ifdef _WIN32
    std::array<char, MAX_PATH> windowsDirectory{};
    const UINT                 directoryLength =
        GetWindowsDirectoryA(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size()));
    CHECK(
        directoryLength != 0 && directoryLength < windowsDirectory.size(),
        "failed to locate the Windows font directory"
    );
    const std::string fontPath = std::string{windowsDirectory.data(), directoryLength} + "\\Fonts\\msyh.ttc";
    CHECK(
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull()) !=
            nullptr,
        "failed to load Microsoft YaHei: {}",
        fontPath
    );
#else
#error RenderLab editor requires Windows to load Microsoft YaHei
#endif

    CHECK(ImGui_ImplGlfw_InitForVulkan(window, true), "failed to init ImGui GLFW backend");
    glfwBackendInitialized = true;

    colorFormat           = targetColorFormat;
    pipelineRenderingInfo = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount    = 1,
        .pColorAttachmentFormats = &colorFormat,
    };

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion                  = VK_API_VERSION_1_4;
    initInfo.Instance                    = instance;
    initInfo.PhysicalDevice              = physicalDevice;
    initInfo.Device                      = device;
    initInfo.QueueFamily                 = queueFamily;
    initInfo.Queue                       = queue;
    initInfo.DescriptorPoolSize          = 64;
    initInfo.MinImageCount               = minImageCount;
    initInfo.ImageCount                  = imageCount;
    initInfo.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering         = true;
    initInfo.PipelineRenderingCreateInfo = pipelineRenderingInfo;

    CHECK(ImGui_ImplVulkan_Init(&initInfo), "failed to init ImGui Vulkan backend");
    vulkanBackendInitialized = true;
}

void EditorBackend::beginFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorBackend::render(VkCommandBuffer commands) const
{
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commands);
}

void EditorBackend::shutdown() noexcept
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
}
