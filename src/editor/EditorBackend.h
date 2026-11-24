#pragma once
#include <array>
#include <slang-rhi.h>
#include "render/RenderConfig.h"
struct GLFWwindow;
class ShaderManager;
class EditorBackend
{
public:
    ~EditorBackend();
    void init(GLFWwindow* window, rhi::IDevice& device, ShaderManager& shaders, rhi::Format colorFormat);
    void beginFrame();
    void render(rhi::IRenderPassEncoder& pass, uint32_t frameIndex);
    void shutdown() noexcept;

private:
    bool                              contextCreated         = false;
    bool                              glfwBackendInitialized = false;
    rhi::IDevice*                     device                 = nullptr;
    rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    rhi::ComPtr<rhi::ITexture>        fontTexture;
    rhi::ComPtr<rhi::ITextureView>    fontView;
    rhi::ComPtr<rhi::ISampler>        fontSampler;
    struct Frame
    {
        rhi::ComPtr<rhi::IBuffer> vertices;
        rhi::ComPtr<rhi::IBuffer> indices;
    };
    std::array<Frame, kMaxFramesInFlight> frames;
};
