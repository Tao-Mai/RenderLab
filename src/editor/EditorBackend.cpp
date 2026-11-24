#include "editor/EditorBackend.h"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <slang-rhi.h>
#include <string>
#include "core/Context.h"
#include "core/profiling/Profiler.h"
#include "render/RenderContext.h"
#include "render/device/RhiCheck.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
namespace
{
struct UiParameters
{
    glm::vec2 scale;
    glm::vec2 translate;
};
}  // namespace

EditorBackend::~EditorBackend() = default;
void EditorBackend::init(GLFWwindow* window, rhi::IDevice& target, ShaderManager& shaders, rhi::Format colorFormat)
{

    device = &target;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    contextCreated = true;
    auto& io       = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.BackendRendererName = "RenderLab_slang_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
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

    CHECK(ImGui_ImplGlfw_InitForOther(window, true));
    glfwBackendInitialized = true;
    unsigned char* pixels;
    int            width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    rhi::TextureDesc textureDesc;
    textureDesc.size   = {uint32_t(width), uint32_t(height), 1};
    textureDesc.format = rhi::Format::RGBA8Unorm;
    textureDesc.usage  = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination;
    const rhi::SubresourceData fontData{pixels, size_t(width) * 4, size_t(width) * height * 4};
    rhiCheck(device->createTexture(textureDesc, &fontData, fontTexture.writeRef()));
    rhiCheck(fontTexture->getDefaultView(fontView.writeRef()));
    io.Fonts->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(fontView.get())));
    rhi::SamplerDesc sampler;
    sampler.minFilter = sampler.magFilter = sampler.mipFilter = rhi::TextureFilteringMode::Linear;
    sampler.addressU = sampler.addressV = sampler.addressW = rhi::TextureAddressingMode::ClampToEdge;
    rhiCheck(device->createSampler(sampler, fontSampler.writeRef()));
    const std::array elements{
        rhi::InputElementDesc{"POSITION", 0, rhi::Format::RG32Float, offsetof(ImDrawVert, pos)},
        rhi::InputElementDesc{"TEXCOORD", 0, rhi::Format::RG32Float, offsetof(ImDrawVert, uv)},
        rhi::InputElementDesc{"COLOR", 0, rhi::Format::RGBA8Unorm, offsetof(ImDrawVert, col)}
    };
    const rhi::VertexStreamDesc stream{sizeof(ImDrawVert)};
    rhi::InputLayoutDesc        inputDesc;
    inputDesc.inputElements     = elements.data();
    inputDesc.inputElementCount = uint32_t(elements.size());
    inputDesc.vertexStreams     = &stream;
    inputDesc.vertexStreamCount = 1;
    rhi::ComPtr<rhi::IInputLayout> input;
    rhiCheck(device->createInputLayout(inputDesc, input.writeRef()));
    rhi::ColorTargetDesc color;
    color.format      = colorFormat;
    color.enableBlend = true;
    color.color       = {rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::InvSrcAlpha, rhi::BlendOp::Add};
    color.alpha       = {rhi::BlendFactor::One, rhi::BlendFactor::InvSrcAlpha, rhi::BlendOp::Add};
    rhi::RenderPipelineDesc desc;
    desc.program                       = shaders.program(shaders.getOrLoad("imgui"));
    desc.inputLayout                   = input;
    desc.targets                       = &color;
    desc.targetCount                   = 1;
    desc.depthStencil.depthTestEnable  = false;
    desc.depthStencil.depthWriteEnable = false;
    desc.rasterizer.cullMode           = rhi::CullMode::None;
    desc.rasterizer.scissorEnable      = true;
    rhiCheck(device->createRenderPipeline(desc, pipeline.writeRef()));
}

void EditorBackend::beginFrame()
{
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}
void EditorBackend::render(rhi::IRenderPassEncoder& pass, uint32_t frameIndex)
{
    const auto* data   = ImGui::GetDrawData();
    const auto  width  = uint32_t(data->DisplaySize.x * data->FramebufferScale.x);
    const auto  height = uint32_t(data->DisplaySize.y * data->FramebufferScale.y);
    if (!width || !height || !data->TotalVtxCount || !data->TotalIdxCount)
        return;
    auto&      frame       = frames[frameIndex];
    const auto vertexBytes = size_t(data->TotalVtxCount) * sizeof(ImDrawVert);
    const auto indexBytes  = size_t(data->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!frame.vertices || frame.vertices->getDesc().size < vertexBytes)
    {
        rhi::BufferDesc desc;
        desc.size       = vertexBytes;
        desc.memoryType = rhi::MemoryType::Upload;
        desc.usage      = rhi::BufferUsage::VertexBuffer;
        rhiCheck(device->createBuffer(desc, nullptr, frame.vertices.writeRef()));
    }
    if (!frame.indices || frame.indices->getDesc().size < indexBytes)
    {
        rhi::BufferDesc desc;
        desc.size       = indexBytes;
        desc.memoryType = rhi::MemoryType::Upload;
        desc.usage      = rhi::BufferUsage::IndexBuffer;
        rhiCheck(device->createBuffer(desc, nullptr, frame.indices.writeRef()));
    }
    void* vertexData;
    void* indexData;
    rhiCheck(device->mapBuffer(frame.vertices, rhi::CpuAccessMode::Write, &vertexData));
    rhiCheck(device->mapBuffer(frame.indices, rhi::CpuAccessMode::Write, &indexData));
    auto* vertices = static_cast<std::byte*>(vertexData);
    auto* indices  = static_cast<std::byte*>(indexData);
    for (const auto* list : data->CmdLists)
    {
        const auto vertexSize = size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const auto indexSize  = size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        std::memcpy(vertices, list->VtxBuffer.Data, vertexSize);
        vertices += vertexSize;
        std::memcpy(indices, list->IdxBuffer.Data, indexSize);
        indices += indexSize;
    }
    rhiCheck(device->unmapBuffer(frame.vertices));
    rhiCheck(device->unmapBuffer(frame.indices));

    rhi::RenderState    state;
    rhi::IShaderObject* object     = nullptr;
    const auto          setupState = [&]
    {
        object = pass.bindPipeline(pipeline);
        CHECK(object);
        state.vertexBufferCount = 1;
        state.vertexBuffers[0]  = {frame.vertices, 0};
        state.indexBuffer       = {frame.indices, 0};
        state.indexFormat       = sizeof(ImDrawIdx) == 2 ? rhi::IndexFormat::Uint16 : rhi::IndexFormat::Uint32;
        state.viewportCount     = 1;
        state.viewports[0]      = rhi::Viewport::fromSize(float(width), float(height));
        state.scissorRectCount  = 1;
        const UiParameters parameters{
            {2.0f / data->DisplaySize.x, -2.0f / data->DisplaySize.y},
            {-1.0f - data->DisplayPos.x * 2.0f / data->DisplaySize.x,
             1.0f + data->DisplayPos.y * 2.0f / data->DisplaySize.y}
        };
        const rhi::ShaderCursor root(object);
        rhiCheck(root["parameters"].getDereferenced().setData(parameters));
        rhiCheck(root["linearSampler"].setBinding(fontSampler));
    };
    setupState();
    uint32_t vertexOffset = 0, indexOffset = 0;
    for (const auto* list : data->CmdLists)
    {
        for (const auto& draw : list->CmdBuffer)
        {
            if (draw.UserCallback)
            {
                if (draw.UserCallback == ImDrawCallback_ResetRenderState)
                    setupState();
                else
                    draw.UserCallback(list, &draw);
                continue;
            }
            const auto minX =
                std::clamp((draw.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x, 0.0f, float(width));
            const auto minY =
                std::clamp((draw.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y, 0.0f, float(height));
            const auto maxX =
                std::clamp((draw.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x, 0.0f, float(width));
            const auto maxY =
                std::clamp((draw.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y, 0.0f, float(height));
            if (maxX <= minX || maxY <= minY)
                continue;
            state.scissorRects[0] = {uint32_t(minX), uint32_t(minY), uint32_t(maxX), uint32_t(maxY)};
            pass.setRenderState(state);

            rhiCheck(
                rhi::ShaderCursor(object)["sourceImage"].setBinding(
                    reinterpret_cast<rhi::ITextureView*>(static_cast<uintptr_t>(draw.GetTexID()))
                )
            );
            pass.drawIndexed({draw.ElemCount, 1, vertexOffset + draw.VtxOffset, 0, indexOffset + draw.IdxOffset});
            recordDraw(context().profiler, draw.ElemCount);
        }
        vertexOffset += list->VtxBuffer.Size;
        indexOffset += list->IdxBuffer.Size;
    }
}

void EditorBackend::shutdown() noexcept
{
    frames = {};
    pipeline.setNull();
    fontView.setNull();
    fontTexture.setNull();
    fontSampler.setNull();
    device = nullptr;
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
