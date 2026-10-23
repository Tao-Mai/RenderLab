// Targeted GPU check: HDR preservation, glow, switch-off, and viewport isolation.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/feature/postprocess/BloomPass.h"
#include "render/resource/Buffer.h"
#include "scene/SceneManager.h"

#include <array>
#include <cmath>
#include <glm/gtc/packing.hpp>

namespace
{
struct HdrSourcePass
{
    struct Output
    {
        RenderGraph::ImageHandle color;
    };
    RenderGraph::ImageHandle color;
    bool                     uniform = false;
    vk::Offset2D             lightCenter;

    Output setup(RenderGraph& graph, const PassSetup& setup)
    {
        const auto extent = setup.renderExtent;
        color = graph.write(graph.createImage({.format = kHdrColorFormat, .extent = {extent.width, extent.height, 1}}));
        graph.use(
            color,
            {.stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
             .access = vk::AccessFlagBits2::eColorAttachmentWrite,
             .layout = vk::ImageLayout::eColorAttachmentOptimal}
        );
        return {color};
    }

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
    {
        const auto                        extent = graph.imageExtent(color);
        const vk::RenderingAttachmentInfo attachment{
            .imageView = graph.getImage(color).getOrCreateImageView(
                {.viewType         = vk::ImageViewType::e2D,
                 .format           = kHdrColorFormat,
                 .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1, .layerCount = 1}}
            ),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp      = vk::AttachmentLoadOp::eClear,
            .storeOp     = vk::AttachmentStoreOp::eStore,
            .clearValue  = uniform ? vk::ClearColorValue(4.0f, 2.0f, 1.0f, 1.0f) : vk::ClearColorValue(0, 0, 0, 1),
        };
        auto& commands = commandBuffer;
        commands.beginRendering(
            {.renderArea           = {{0, 0}, extent},
             .layerCount           = 1,
             .colorAttachmentCount = 1,
             .pColorAttachments    = &attachment}
        );
        if (!uniform)
        {
            const vk::ClearAttachment clear{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .clearValue = vk::ClearColorValue(16.0f, 4.0f, 0.0f, 1.0f)
            };
            const vk::ClearRect rect{.rect = {{lightCenter.x - 4, lightCenter.y - 4}, {8, 8}}, .layerCount = 1};
            commands.clearAttachments(clear, rect);
        }
        commands.endRendering();
    }
};

struct ReadPixelPass
{
    RenderGraph::ImageHandle  source;
    RenderGraph::BufferHandle buffer;
    vk::Offset3D              pixel;

    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup& setup, RenderGraph::ImageHandle image)
    {
        source = image;
        buffer = graph.write(graph.createBuffer(
            {.size             = sizeof(uint16_t) * 4,
             .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
             .scope            = RenderGraph::ResourceScope::PerFrame}
        ));
        graph.use(
            source,
            {.stages = vk::PipelineStageFlagBits2::eTransfer,
             .access = vk::AccessFlagBits2::eTransferRead,
             .layout = vk::ImageLayout::eTransferSrcOptimal}
        );
        graph.use(
            buffer,
            {.stages = vk::PipelineStageFlagBits2::eTransfer, .access = vk::AccessFlagBits2::eTransferWrite}
        );
        return buffer;
    }

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commandBuffer, const PassFrame& execution) const
    {
        const vk::BufferImageCopy copy{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageOffset      = pixel,
            .imageExtent      = {1, 1, 1}
        };
        commandBuffer.copyImageToBuffer(
            graph.getImage(source).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer, execution.frameIndex).handle(),
            copy
        );
    }
};
}  // namespace

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    Window           window;
    Renderer         renderer;
    Profiler         profiler;
    auto&            ctx = context();
    ctx.config           = &config;
    ctx.assetManager     = &assets;
    ctx.assetDataManager = &data;
    ctx.sceneManager     = &scenes;
    ctx.window           = &window;
    ctx.renderer         = &renderer;
    ctx.profiler         = &profiler;
    config.init();
    const auto original = config.rendererConfig().feature;
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();

    auto&                                       vulkan = renderer.vulkanContext();
    std::array<FrameContext, maxFramesInFlight> frames;
    std::array<FrameData, maxFramesInFlight>    frameData;
    for (auto& frame : frames)
        frame.init(vulkan);
    for (auto& frame : frameData)
        frame.init(vulkan, *renderContext().device);
    RenderGraph graph;
    graph.init(vulkan, renderer.swapchainHandle());
    const PassSetup setup{vulkan, renderer.swapchainHandle().extent(), frameData, context().config->rendererConfig()};
    HdrSourcePass   source;
    ReadPixelPass   readback;
    EndPass         end;
    BloomPass       bloom;
    const auto      extent = renderer.swapchainHandle().extent();
    CHECK(extent.width > 32 && extent.height > 32);
    const int  centerX = static_cast<int>(extent.width / 2);
    const int  centerY = static_cast<int>(extent.height / 2);
    RenderView view{.viewport = {0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)}};
    source.lightCenter = vk::Offset2D{centerX, centerY};
    readback.pixel     = vk::Offset3D{centerX + 8, centerY, 0};

    const auto sample =
        [&](bool enabled, float intensity = 0.04f, uint32_t levels = 5, bool hdr = true, float scatter = 0.7f)
    {
        auto feature = original;
        feature.hdr  = hdr;
        feature.bloom =
            {.enable = enabled, .intensity = intensity, .scatter = scatter, .filterRadius = 1, .levels = levels};
        config.setRenderFeature(feature);
        graph.releaseResources();
        auto color = graph.addPass(source, setup).color;
        if (feature.hdr && feature.bloom.enable)
            color = graph.addPass(bloom, setup, color).color;
        const auto buffer = graph.addPass(readback, setup, color);
        graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
        graph.compile(setup);
        profiler.reset();
        profiler.beginFrame();
        auto& recording = frames[0].commandBufferHandle();
        vkCheck(recording.reset());
        vkCheck(recording.begin({}));
        graph
            .execute(recording, {setup, frameData[0], view, {}, {}, 0}, 0, frames[0].gpuProfiler(), context().profiler);
        vkCheck(recording.end());
        const auto command = *frames[0].commandBufferHandle();
        vkCheck(vulkan.queueHandle().submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command}));
        vkCheck(vulkan.queueHandle().waitIdle());
        frames[0].gpuProfiler().collect(ctx.profiler);
        profiler.endFrame(1.0);

        DrawStatistics counts;
        uint32_t       bloomPassCount = 0;
        for (const auto& pass : profiler.display().passes)
        {
            if (pass.name == "FilterPass")
            {
                ++bloomPassCount;
                CHECK(pass.draws.drawCalls == 1);
            }
            counts.drawCalls += pass.draws.drawCalls;
            counts.triangles += pass.draws.triangles;
        }
        const uint64_t expectedDraws = ctx.profiler && hdr && enabled ? 3 * levels : 0;
        CHECK(counts.drawCalls == expectedDraws);
        CHECK(counts.triangles == expectedDraws);
        CHECK(bloomPassCount == expectedDraws);

        std::array<uint16_t, 4> pixel;
        graph.getBuffer(buffer, 0).download(pixel.data(), sizeof(pixel));
        const glm::vec3 result{
            glm::unpackHalf1x16(pixel[0]),
            glm::unpackHalf1x16(pixel[1]),
            glm::unpackHalf1x16(pixel[2])
        };
        CHECK(std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z));
        return result;
    };

    CHECK(sample(false).x == 0);
    const auto glow = sample(true);
    CHECK(glow.x > 0 && glow.y > 0 && glow.z == 0);
    CHECK(sample(true, 0).x == 0);
    readback.pixel.x = centerX + 5;
    CHECK(sample(true, 0.04f, 1).x > 0);
    readback.pixel.x = centerX + 8;
    CHECK(sample(true, 0.04f, 5, false).x == 0);
    readback.pixel.x = centerX + 16;
    CHECK(sample(true, 0.04f, 5, true, 0.95f).x > sample(true, 0.04f, 5, true, 0.05f).x);
    readback.pixel.x = centerX + 8;
    source.uniform   = true;
    CHECK(glm::length(sample(true) - glm::vec3{4, 2, 1}) < 0.01f);
    source.uniform       = false;
    source.lightCenter.x = centerX - 4;
    view.viewport =
        {static_cast<float>(centerX), 0, static_cast<float>(extent.width - centerX), static_cast<float>(extent.height)};
    CHECK(sample(true).x == 0);
    ctx.profiler   = nullptr;
    source.uniform = true;
    view.viewport  = {0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)};
    CHECK(glm::length(sample(true) - glm::vec3{4, 2, 1}) < 0.01f);
    LOG_INFO("Bloom GPU checks passed; glow RGB = {}, {}, {}", glow.x, glow.y, glow.z);

    graph.reset();
    bloom = {};
    for (auto& frame : frameData)
        frame.reset();
    for (auto& frame : frames)
        frame.reset();
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setRenderFeature(original);
    config.shutdown();
    ctx = {};
}
