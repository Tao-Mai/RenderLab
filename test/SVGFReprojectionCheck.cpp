// Targeted GPU checks for SVGF accumulation, variance, wavelet filtering and history feedback.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/raytracing/SVGFFilter.h"
#include "render/raytracing/SVGFReprojection.h"
#include "scene/SceneManager.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/packing.hpp>

namespace
{
using Pixel                            = std::array<float, 4>;
constexpr uint32_t kReadbackImageCount = 6;
constexpr uint32_t kSourceImageCount   = 6;
constexpr uint32_t kTestImageSize      = 32;

struct SourcePass
{
    RayTracingPass::Output    output;
    float                     value = 1.0f;
    float                     depth = 0.5f;
    glm::vec4                 normal{0, 0, 1, 0};
    bool                      noisy      = false;
    bool                      impulse    = false;
    bool                      depthEdge  = false;
    bool                      normalEdge = false;
    glm::vec3                 albedo{1.0f};
    bool                      albedoChecker = false;
    bool                      modulate      = false;
    RenderGraph::BufferHandle uploadBuffer;
    vk::Extent2D              extent{};

    RayTracingPass::Output setup(RenderGraph& graph, const PassSetup& setup)
    {
        extent            = setup.renderExtent;
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
        output = {
            .depth       = create(vk::Format::eR32Sfloat),
            .worldNormal = create(kHdrColorFormat),
            .direct      = create(kHdrColorFormat),
            .indirect    = create(kHdrColorFormat),
            .emission    = create(kHdrColorFormat),
            .albedo      = create(kHdrColorFormat),
        };
        uploadBuffer = graph.createBuffer({
            .size  = vk::DeviceSize(extent.width) * extent.height * kSourceImageCount * sizeof(uint64_t),
            .usage = vk::BufferUsageFlagBits::eTransferDst,
        });
        graph.use(
            uploadBuffer,
            {.stages = vk::PipelineStageFlagBits2::eTransfer, .access = vk::AccessFlagBits2::eTransferRead}
        );
        return output;
    }

    void prepare(RenderGraph& graph, const PassFrame&) const
    {
        const auto          planeBytes = vk::DeviceSize(extent.width) * extent.height * sizeof(uint64_t);
        const auto          upload     = graph.allocateUpload(uploadBuffer, planeBytes * kSourceImageCount);
        constexpr glm::vec3 kCheckerAlbedo{0.125f, 0.5f, 0.25f};
        for (uint32_t y = 0; y < extent.height; ++y)
            for (uint32_t x = 0; x < extent.width; ++x)
            {
                const auto index      = y * extent.width + x;
                const bool right      = x >= extent.width / 2;
                float      pixelValue = value;
                if (noisy && (x + y) % 2 != 0)
                    pixelValue = 0;
                if (impulse && (x != extent.width / 2 || y != extent.height / 2))
                    pixelValue = 0;
                if ((depthEdge || normalEdge) && right)
                    pixelValue = 0;
                const auto writeColor = [&](uint32_t plane, glm::vec4 color)
                {
                    const auto packed = glm::packHalf4x16(color);
                    std::memcpy(
                        upload.data.data() + plane * planeBytes + index * sizeof(packed),
                        &packed,
                        sizeof(packed)
                    );
                };
                const auto pixelAlbedo = albedoChecker && (x + y) % 2 != 0 ? kCheckerAlbedo : albedo;
                const auto factor      = modulate ? pixelAlbedo : glm::vec3{1.0f};
                writeColor(0, {glm::vec3{pixelValue, 0, 0} * factor, 1});
                writeColor(1, {glm::vec3{0, 2 * pixelValue + 1, 0} * factor, 1});
                writeColor(2, {0, 0, 3 * value, 1});
                const float pixelDepth = depthEdge && right ? 2 * depth : depth;
                std::memcpy(upload.data.data() + 3 * planeBytes + index * sizeof(float), &pixelDepth, sizeof(float));
                writeColor(4, normalEdge && right ? glm::vec4{0, 1, 0, 0} : normal);
                writeColor(5, {pixelAlbedo, 1});
            }
    }

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame&) const
    {
        const std::array
            handles{output.direct, output.indirect, output.emission, output.depth, output.worldNormal, output.albedo};
        const auto planeBytes = vk::DeviceSize(extent.width) * extent.height * sizeof(uint64_t);
        for (size_t index = 0; index < handles.size(); ++index)
        {
            const vk::BufferImageCopy copy{
                .bufferOffset     = index * planeBytes,
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
                .imageExtent      = {extent.width, extent.height, 1}
            };
            commands.copyBufferToImage(
                graph.getBuffer(uploadBuffer).handle(),
                graph.getImage(handles[index]).handle(),
                vk::ImageLayout::eTransferDstOptimal,
                copy
            );
        }
    }
};

struct ReadbackImages
{
    RenderGraph::ImageHandle direct;
    RenderGraph::ImageHandle indirect;
    RenderGraph::ImageHandle directMoments;
    RenderGraph::ImageHandle indirectMoments;
};

struct RawReadbackPass
{
    explicit RawReadbackPass(const SVGFReprojection& reprojection) : reprojection(reprojection) {}
    const SVGFReprojection&                 reprojection;
    std::array<RenderGraph::ImageHandle, 4> images;
    RenderGraph::BufferHandle               buffer;
    RenderGraph::BufferHandle               setup(RenderGraph& graph, const PassSetup&, ReadbackImages source)
    {
        images = {source.direct, source.indirect, source.directMoments, source.indirectMoments};
        buffer = graph.write(graph.createBuffer(
            {.size             = sizeof(Pixel) * maxFramesInFlight * kReadbackImageCount,
             .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent}
        ));
        for (const auto image : images)
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
        const auto extent = graph.imageInfo(images[0]).extent;
        for (uint32_t index = 0; index < images.size(); ++index)
        {
            const vk::BufferImageCopy copy{
                .bufferOffset = sizeof(Pixel) * (frame.frameIndex * kReadbackImageCount + index),
                .imageSubresource =
                    {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                     .baseArrayLayer = index >= 2 ? reprojection.colorHistoryLayer() : 0u,
                     .layerCount     = 1},
                .imageOffset = {static_cast<int>(extent.width / 2), static_cast<int>(extent.height / 2), 0},
                .imageExtent = {1, 1, 1},
            };
            commands.copyImageToBuffer(
                graph.getImage(images[index]).handle(),
                vk::ImageLayout::eTransferSrcOptimal,
                graph.getBuffer(buffer).handle(),
                copy
            );
        }
    }
};

struct FinalReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;

    RenderGraph::BufferHandle
    setup(RenderGraph& graph, const PassSetup&, RenderGraph::ImageHandle color, RenderGraph::BufferHandle rawResults)
    {
        image  = color;
        buffer = graph.write(rawResults);
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
        const auto extent = graph.imageInfo(image).extent;
        for (uint32_t offset = 0; offset < 2; ++offset)
        {
            const vk::BufferImageCopy copy{
                .bufferOffset     = sizeof(Pixel) * (frame.frameIndex * kReadbackImageCount + 4 + offset),
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
                .imageOffset = {static_cast<int>(extent.width / 2 + offset), static_cast<int>(extent.height / 2), 0},
                .imageExtent = {1, 1, 1},
            };
            commands.copyImageToBuffer(
                graph.getImage(image).handle(),
                vk::ImageLayout::eTransferSrcOptimal,
                graph.getBuffer(buffer).handle(),
                copy
            );
        }
    }
};

void check(Renderer& renderer)
{
    auto&      config                = *context().config;
    const auto originalFeature       = config.rendererConfig().feature;
    auto       feature               = originalFeature;
    feature.rayTracing.maxBounces    = kMinPathBounces + 1;
    feature.rayTracing.svgf          = true;
    feature.rayTracing.historyLength = 1;
    feature.rayTracing.emaAlpha      = 0.1f;
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
    SourcePass                source;
    SVGFReprojection          reprojection;
    SVGFFilter                filter;
    RawReadbackPass           rawReadback{reprojection};
    FinalReadbackPass         finalReadback;
    EndPass                   end;
    const PassSetup           setup{vulkan, {kTestImageSize, kTestImageSize}, data, context().config->rendererConfig()};
    RenderGraph::BufferHandle buffer;
    const auto                build = [&]
    {
        graph.releaseResources();
        const auto input      = graph.addPass(source, setup);
        const auto lighting   = graph.addPass(reprojection, setup, input);
        const auto rawResults = graph.addPass(
            rawReadback,
            setup,
            ReadbackImages{lighting.direct, lighting.indirect, lighting.directMoments, lighting.indirectMoments}
        );
        const auto color = graph.addPass(filter, setup, input, lighting, reprojection).color;
        buffer           = graph.addPass(finalReadback, setup, color, rawResults);
        graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
        graph.compile(setup);
    };
    build();
    const auto extent = setup.renderExtent;
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
    const auto readPixel = [&](uint32_t index, uint32_t image)
    {
        // Download all test-owned slots after every pending writer has completed.
        for (auto& frame : frames)
            frame.wait();
        std::array<std::array<Pixel, kReadbackImageCount>, maxFramesInFlight> pixels;
        graph.getBuffer(buffer).download(pixels.data(), sizeof(pixels));
        return pixels[index][image];
    };
    const auto read   = [&](uint32_t index) { return glm::unpackHalf1x16(glm::packHalf1x16(readPixel(index, 0)[0])); };
    const auto sample = [&](float value) { return read(submit(value)); };
    float      expectedMean = 1.0f;
    CHECK(sample(1) == expectedMean);
    for (int count = 2; count <= 10; ++count)
    {
        expectedMean += (float(count) - expectedMean) * feature.rayTracing.emaAlpha;
        CHECK(std::abs(sample(float(count)) - expectedMean) < 0.01f);
    }
    expectedMean += (20.0f - expectedMean) * feature.rayTracing.emaAlpha;
    CHECK(std::abs(sample(20) - expectedMean) < 0.01f);
    view.uniforms.inverseViewProjection[3].x = 0.1f;  // Reconstructed positions move sideways at equal depth.
    expectedMean += (10.0f - expectedMean) * feature.rayTracing.emaAlpha;
    CHECK(std::abs(sample(10) - expectedMean) < 0.01f);
    source.normal = {1, 0, 0, 0};
    CHECK(sample(30) == 30);
    CHECK(sample(40) == 31);
    source.depth = 0.7f;
    CHECK(sample(50) == 50);
    view.uniforms.viewProjection[3].x = 4;
    CHECK(sample(60) == 51);  // Reprojects using the previous camera, not the current one.
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
    CHECK(sample(120) == 102);        // Camera depth changed, but comparison still uses the previous view.
    CHECK(sample(142) == 106);        // Keep this EMA result exactly representable in the RGBA16F output.

    feature.rayTracing.emaAlpha = 0.25f;
    config.setRenderFeature(feature);
    build();
    CHECK(sample(2) == 2);
    CHECK(sample(4) == 2.5f);
    CHECK(sample(10) == 4.375f);
    feature.rayTracing.emaAlpha = 0.5f;
    config.setRenderFeature(feature);
    CHECK(sample(10) == 7.1875f);  // Weight changes apply without rebuilding the graph.
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

    feature.rayTracing.historyLength = 3;
    feature.rayTracing.emaAlpha      = 0.5f;
    config.setRenderFeature(feature);
    build();
    CHECK(sample(2) == 2);
    CHECK(sample(4) == 3);
    CHECK(sample(6) == 4);        // The third sample still uses arithmetic averaging.
    const auto fourth = submit(10);
    CHECK(read(fourth) == 7.0f);  // The fourth sample exceeds historyLength; EMA differs from 1/4 arithmetic weighting.
    CHECK(std::abs(readPixel(fourth, 1)[1] - 15.0f) < 0.001f);
    const auto directMoments   = readPixel(fourth, 2);
    const auto indirectMoments = readPixel(fourth, 3);
    CHECK(std::abs(directMoments[0] - 7.0f * 0.2126f) < 0.0001f);
    CHECK(std::abs(directMoments[1] - (178.0f / 3.0f) * 0.2126f * 0.2126f) < 0.0001f);
    CHECK(std::abs(indirectMoments[0] - 15.0f * 0.7152f) < 0.001f);
    CHECK(std::abs(indirectMoments[1] - (799.0f / 3.0f) * 0.7152f * 0.7152f) < 0.001f);
    const auto combined = std::bit_cast<std::array<uint16_t, 8>>(readPixel(fourth, 4));
    CHECK(glm::unpackHalf1x16(combined[0]) == 7.0f);
    CHECK(std::abs(glm::unpackHalf1x16(combined[1]) - 15.0f) < 0.01f);
    CHECK(glm::unpackHalf1x16(combined[2]) == 30.0f);  // Emission is current-frame data, not accumulated.

    source.normal       = {0, 1, 0, 0};
    const auto rejected = submit(12);
    CHECK(read(rejected) == 12);
    CHECK(std::abs(readPixel(rejected, 1)[1] - 25) < 0.001f);
    const auto resetMoments = readPixel(rejected, 2);
    CHECK(std::abs(resetMoments[0] - 12.0f * 0.2126f) < 0.0001f);
    CHECK(std::abs(resetMoments[1] - 144.0f * 0.2126f * 0.2126f) < 0.0001f);

    feature.rayTracing.historyLength = 32;
    config.setRenderFeature(feature);
    source.normal = {0, 0, 1, 0};
    source.noisy  = true;
    build();
    const auto noisyFrame = submit(10);
    const auto raw        = readPixel(noisyFrame, 0);
    CHECK(raw[3] > 0);  // One frame has zero temporal variance, so the 7x7 spatial estimate must be used.
    const auto  denoised    = std::bit_cast<std::array<uint16_t, 8>>(readPixel(noisyFrame, 4));
    const float filteredRed = glm::unpackHalf1x16(denoised[0]);
    CHECK(std::abs(filteredRed - 5.0f) < std::abs(raw[0] - 5.0f));
    CHECK(glm::unpackHalf1x16(denoised[2]) == 30);  // Emission bypasses all wavelet levels.
    sample(10);
    sample(10);
    const auto mature = submit(10);
    CHECK(std::abs(readPixel(mature, 0)[3]) < 0.0001f);  // Four identical temporal samples replace spatial variance.

    source.noisy   = false;
    source.impulse = true;
    build();
    sample(10);
    const auto  feedback    = submit(0);
    const float feedbackRed = readPixel(feedback, 0)[0];
    CHECK(feedbackRed > 0 && feedbackRed < 5.0f);  // Temporal color uses the first filtered frame, not raw 10.

    source.impulse   = false;
    source.depthEdge = true;
    build();
    const auto depthBoundary = submit(10);
    CHECK(glm::unpackHalf1x16(std::bit_cast<std::array<uint16_t, 8>>(readPixel(depthBoundary, 4))[0]) == 0);
    source.depthEdge  = false;
    source.normalEdge = true;
    build();
    const auto normalBoundary = submit(10);
    CHECK(glm::unpackHalf1x16(std::bit_cast<std::array<uint16_t, 8>>(readPixel(normalBoundary, 4))[0]) == 0);
    view.viewport = {float(extent.width / 2), 0, float(extent.width / 2), float(extent.height)};
    source.normal = {0, 1, 0, 0};  // Both sides now share geometry; only the viewport rejects the bright side.
    build();
    const auto viewportBoundary = submit(10);
    CHECK(glm::unpackHalf1x16(std::bit_cast<std::array<uint16_t, 8>>(readPixel(viewportBoundary, 4))[0]) == 0);

    view.viewport     = {0, 0, float(extent.width), float(extent.height)};
    source.normal     = {0, 0, 1, 0};
    source.normalEdge = false;
    for (const float brightness : {128.0f, 512.0f, 1024.0f, 4096.0f, 16384.0f})
    {
        build();
        const auto uniform = submit(brightness);
        CHECK(readPixel(uniform, 0)[3] == 0);
        CHECK(readPixel(uniform, 1)[3] == 0);
    }
    source.albedo = {0.5f, 0.25f, 0.125f};
    build();
    const auto demodulated = submit(2);
    CHECK(std::abs(readPixel(demodulated, 0)[0] - 4.0f) < 0.0001f);
    CHECK(std::abs(readPixel(demodulated, 1)[1] - 20.0f) < 0.0001f);
    const auto demodulatedMoments = readPixel(demodulated, 2);
    CHECK(std::abs(demodulatedMoments[0] - 4.0f * 0.2126f) < 0.0001f);
    CHECK(std::abs(demodulatedMoments[1] - 16.0f * 0.2126f * 0.2126f) < 0.0001f);
    const auto remodulated = std::bit_cast<std::array<uint16_t, 8>>(readPixel(demodulated, 4));
    CHECK(glm::unpackHalf1x16(remodulated[0]) == 2);
    CHECK(glm::unpackHalf1x16(remodulated[1]) == 5);
    CHECK(glm::unpackHalf1x16(remodulated[2]) == 6);  // Emission is independent of material color.

    source.albedo = {0, 0.5f, 0};
    build();
    const auto blackChannels = submit(2);
    CHECK(std::isfinite(readPixel(blackChannels, 0)[0]));
    CHECK(std::isfinite(readPixel(blackChannels, 0)[3]));
    const auto blackResult = std::bit_cast<std::array<uint16_t, 8>>(readPixel(blackChannels, 4));
    CHECK(std::abs(glm::unpackHalf1x16(blackResult[0]) - 2.0f) < 0.002f);
    CHECK(glm::unpackHalf1x16(blackResult[2]) == 6);

    source.albedo   = {0.5f, 0.25f, 0.125f};
    source.modulate = true;
    build();
    CHECK(sample(2) == 2);
    source.albedo            = {0.25f, 0.5f, 0.5f};
    const auto changedAlbedo = submit(2);
    CHECK(std::abs(readPixel(changedAlbedo, 0)[0] - 2.0f) < 0.0001f);
    CHECK(std::abs(readPixel(changedAlbedo, 2)[0] - 2.0f * 0.2126f) < 0.0001f);
    CHECK(std::abs(readPixel(changedAlbedo, 2)[1] - 4.0f * 0.2126f * 0.2126f) < 0.0001f);
    const auto changedResult = std::bit_cast<std::array<uint16_t, 8>>(readPixel(changedAlbedo, 4));
    CHECK(glm::unpackHalf1x16(changedResult[0]) == 0.5f);  // Demodulated history is reused with current albedo.
    CHECK(glm::unpackHalf1x16(changedResult[1]) == 2.5f);

    source.albedo        = {0.5f, 0.25f, 0.125f};
    source.albedoChecker = true;
    build();
    const auto textureFrame = submit(2);
    CHECK(std::abs(readPixel(textureFrame, 0)[3]) < 0.0001f);  // Albedo detail creates no illumination variance.
    CHECK(std::abs(readPixel(textureFrame, 1)[3]) < 0.0001f);
    const auto textureCenter   = std::bit_cast<std::array<uint16_t, 8>>(readPixel(textureFrame, 4));
    const auto textureNeighbor = std::bit_cast<std::array<uint16_t, 8>>(readPixel(textureFrame, 5));
    CHECK(glm::unpackHalf1x16(textureCenter[0]) == 1.0f);
    CHECK(glm::unpackHalf1x16(textureCenter[1]) == 1.25f);
    CHECK(glm::unpackHalf1x16(textureNeighbor[0]) == 0.25f);
    CHECK(glm::unpackHalf1x16(textureNeighbor[1]) == 2.5f);

    config.setRenderFeature(originalFeature);
    LOG_INFO(
        "SVGF passed: direct/indirect accumulation, arithmetic/EMA moments, temporal/spatial variance, wavelet noise "
        "reduction, first-iteration history feedback, depth/normal/viewport edges, emission fusion, history rejection "
        "and frames in flight, albedo demodulation/remodulation, black channels, texture detail, material changes, "
        "bright uniform variance and reused filter images"
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
