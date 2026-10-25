// Targeted check: arithmetic average, EMA, nearest history and rejection.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/raytracing/TemporalReprojectionPass.h"
#include "scene/SceneManager.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

namespace
{
using Pixel = std::array<uint16_t, 4>;

struct SourcePass
{
    RayTracingPass::Output output;
    float                  value = 1.0f;
    float                  depth = 0.5f;
    glm::vec4              normal{0, 0, 1, 0};

    RayTracingPass::Output setup(RenderGraph& graph, const PassSetup& setup)
    {
        const auto extent = setup.renderExtent;
        const auto create = [&](vk::Format format)
        {
            auto image = graph.write(graph.createImage({.format = format, .extent = {extent.width, extent.height, 1}}));
            graph.use(
                image,
                {.stages = vk::PipelineStageFlagBits2::eTransfer,
                 .access = vk::AccessFlagBits2::eTransferWrite,
                 .layout = vk::ImageLayout::eTransferDstOptimal}
            );
            return image;
        };
        output = {create(kHdrColorFormat), create(vk::Format::eR32Sfloat), create(kHdrColorFormat)};
        return output;
    }

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const
    {
        const std::array                handles{output.rayImage, output.depth, output.worldNormal};
        const std::array                values{glm::vec4{value, 0, 0, 1}, glm::vec4{depth, 0, 0, 0}, normal};
        const vk::ImageSubresourceRange range{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .levelCount = 1,
            .layerCount = 1
        };
        for (size_t index = 0; index < handles.size(); ++index)
            commands.clearColorImage(
                graph.getImage(handles[index]).handle(),
                vk::ImageLayout::eTransferDstOptimal,
                vk::ClearColorValue{values[index].x, values[index].y, values[index].z, values[index].w},
                range
            );
    }
};

struct ReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;
    RenderGraph::BufferHandle setup(RenderGraph& graph, const PassSetup&, RenderGraph::ImageHandle source)
    {
        image  = source;
        buffer = graph.write(graph.createBuffer(
            {.size             = sizeof(Pixel) * maxFramesInFlight,
             .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent}
        ));
        graph.use(
            image,
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
    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const
    {
        const auto                extent = graph.imageInfo(image).extent;
        const vk::BufferImageCopy copy{
            .bufferOffset     = sizeof(Pixel) * frame.frameIndex,
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageOffset      = {static_cast<int>(extent.width / 2), static_cast<int>(extent.height / 2), 0},
            .imageExtent      = {1, 1, 1},
        };
        commands.copyImageToBuffer(
            graph.getImage(image).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer).handle(),
            copy
        );
    }
};

void check(Renderer& renderer)
{
    auto&      config                      = *context().config;
    const auto originalFeature             = config.rendererConfig().feature;
    auto       feature                     = originalFeature;
    feature.rayTracing.maxBounces          = kMinPathBounces + 1;
    feature.rayTracing.emaStartSampleCount = 10;
    feature.rayTracing.emaAlpha            = 0.1f;
    config.setRenderFeature(feature);

    auto&                                       vulkan = renderer.vulkanContext();
    std::array<FrameContext, maxFramesInFlight> frames;
    std::array<FrameData, maxFramesInFlight>    data;
    for (auto& frame : frames)
        frame.init(vulkan);
    for (auto& frame : data)
        frame.init(vulkan, *renderContext().device);
    RenderGraph graph;
    graph.init(vulkan);
    SourcePass               source;
    TemporalReprojectionPass temporal;
    ReadbackPass             readback;
    EndPass                  end;
    const PassSetup setup{vulkan, renderer.swapchainHandle().extent(), data, context().config->rendererConfig()};
    RenderGraph::BufferHandle buffer;
    const auto                build = [&]
    {
        graph.releaseResources();
        const auto input = graph.addPass(source, setup);
        const auto color = graph.addPass(temporal, setup, input).color;
        buffer           = graph.addPass(readback, setup, color);
        graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
        graph.compile(setup);
    };
    build();
    const auto extent = renderer.swapchainHandle().extent();
    RenderView view{.viewport = {0, 0, float(extent.width), float(extent.height)}};
    view.uniforms.view[2][2] = -1;  // Positive Z world positions have positive camera-forward depth.
    uint32_t   frameNumber   = 0;
    const auto submit        = [&](float value)
    {
        source.value     = value;
        const auto index = frameNumber++ % maxFramesInFlight;
        frames[index].wait();
        data[index].uploadSceneUniforms(view.uniforms);
        const PassFrame execution{setup, data[index], view, {}, {}, index};
        graph.prepare(execution);
        auto& commands = frames[index].commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        graph.execute(commands, execution, frames[index].gpuProfiler(), nullptr);
        vkCheck(commands.end());
        const auto command = *commands;
        vkCheck(vulkan.deviceHandle().resetFences(frames[index].drawFenceHandle()));
        vkCheck(vulkan.queueHandle().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command},
            frames[index].drawFenceHandle()
        ));
        return index;
    };
    const auto read = [&](uint32_t index)
    {
        // Download all test-owned slots after every pending writer has completed.
        for (auto& frame : frames)
            frame.wait();
        std::array<Pixel, maxFramesInFlight> pixels;
        graph.getBuffer(buffer).download(pixels.data(), sizeof(pixels));
        return glm::unpackHalf1x16(pixels[index][0]);
    };
    const auto sample = [&](float value) { return read(submit(value)); };
    for (int count = 1; count <= 10; ++count)
        CHECK(std::abs(sample(float(count)) - (count + 1.0f) * 0.5f) < 0.01f);
    CHECK(std::abs(sample(20) - 6.95f) < 0.01f);
    view.uniforms.inverseViewProjection[3].x = 0.1f;  // Reconstructed positions move sideways at equal depth.
    CHECK(std::abs(sample(10) - 7.255f) < 0.01f);
    source.normal = {1, 0, 0, 0};
    CHECK(sample(30) == 30);
    CHECK(sample(40) == 35);
    source.depth = 0.7f;
    CHECK(sample(50) == 50);
    view.uniforms.viewProjection[3].x = 4;
    CHECK(sample(60) == 55);  // Reprojects using the previous camera, not the current one.
    CHECK(sample(70) == 70);  // The previous camera now projects outside the viewport.
    view.uniforms.viewProjection = glm::mat4{1};
    source.depth                 = -1;
    CHECK(sample(80) == 80);
    source.depth = 0.7f;
    CHECK(sample(90) == 90);
    build();
    CHECK(sample(100) == 100);
    view.uniforms.view[3][2] = -2.0f;
    source.depth             = 2.7f;  // The same surface now has a different raw view-space depth.
    CHECK(sample(120) == 110);        // Camera depth changed, but comparison still uses the previous view.
    CHECK(sample(140) == 120);        // Stored history now uses the new camera's linear depth.

    feature.rayTracing.emaStartSampleCount = 2;
    feature.rayTracing.emaAlpha            = 0.25f;
    config.setRenderFeature(feature);
    build();
    CHECK(sample(2) == 2);
    CHECK(sample(4) == 3);
    CHECK(sample(10) == 4.75f);
    feature.rayTracing.emaAlpha = 0.5f;
    config.setRenderFeature(feature);
    CHECK(sample(10) == 7.375f);  // Weight changes apply without rebuilding the graph.
    feature.rayTracing.maxBounces = kMinPathBounces;
    config.setRenderFeature(feature);
    CHECK(sample(20) == 20);  // A bounce-budget change rejects the previous illumination history.
    CHECK(sample(40) == 30);

    view.uniforms.view = glm::mat4{1};
    view.uniforms.viewProjection =
        glm::perspective(glm::radians(60.0f), float(extent.width) / float(extent.height), 0.1f, 100.0f);
    view.uniforms.inverseViewProjection = glm::inverse(view.uniforms.viewProjection);
    source.depth                        = 10.0f;  // Linear depths above one must remain valid and reproject correctly.
    build();
    CHECK(sample(2) == 2);
    CHECK(sample(4) == 3);

    build();
    const auto first  = submit(2);
    const auto second = submit(4);
    CHECK(first != second);
    CHECK(read(first) == 2);
    CHECK(read(second) == 3);

    config.setRenderFeature(originalFeature);
    LOG_INFO(
        "Temporal reprojection passed: raw linear depth, mean/EMA, equal-depth tangential motion, "
        "normal/depth/viewport/miss rejection, bounce-budget reset, graph reset"
    );
    graph.reset();
    for (auto& frame : data)
        frame.reset();
    for (auto& frame : frames)
        frame.reset();
}
}  // namespace

int main(int, char** argv)
{
    logger::init(argv[0]);
    ConfigManager    config;
    AssetManager     assets;
    AssetDataManager data;
    SceneManager     scenes;
    Window           window;
    Renderer         renderer;
    context().config           = &config;
    context().assetManager     = &assets;
    context().assetDataManager = &data;
    context().sceneManager     = &scenes;
    context().window           = &window;
    context().renderer         = &renderer;
    config.init();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    check(renderer);
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.shutdown();
    context() = {};
}
