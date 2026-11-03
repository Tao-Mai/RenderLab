#include "LightingSetup.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "editor/LightMarkers.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/raster/DepthPrepass.h"
#include "render/raster/GBufferPass.h"
#include "render/raster/DeferredLightingPass.h"
#include "render/graph/EndPass.h"
#include "render/raster/ScenePass.h"
#include "render/raster/SkyboxPass.h"
#include "render/raster/TransparentPass.h"
#include "render/editor/EditorDepthPass.h"
#include "render/editor/LightMarkerPass.h"
#include "render/postprocess/Bloom.h"
#include "render/postprocess/ToneMappingPass.h"
#include "render/postprocess/ExposurePass.h"
#include "render/raytracing/offline/OfflinePathTracingPass.h"
#include "render/raytracing/BlasBuildPass.h"
#include "render/raytracing/realtime/RayTracingPass.h"
#include "render/raytracing/realtime/SVGFReprojection.h"
#include "render/raytracing/realtime/SVGFFilter.h"
#include "render/raytracing/TlasBuildPass.h"
#include "render/resources/MaterialTable.h"
#include "scene/SceneManager.h"
#include "scene/system/RenderSystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stb_image_write.h>
#include <string_view>

namespace
{
template <class T>
T parseNumber(std::string_view text)
{
    T          value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    CHECK(result.ec == std::errc{} && result.ptr == text.data() + text.size(), "invalid number: {}", text);
    return value;
}

struct ImageReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;
    RenderGraph::BufferHandle exposure;
    RenderGraph::BufferHandle triangles;
    vk::DeviceSize            imageBytes;
    vk::DeviceSize            triangleBytes = 0;

    RenderGraph::BufferHandle setup(
        RenderGraph& graph,
        const PassSetup&,
        RenderGraph::ImageHandle  source,
        RenderGraph::BufferHandle exposureState,
        RenderGraph::BufferHandle triangleLights
    )
    {
        image             = source;
        const auto extent = graph.imageInfo(image).extent;
        exposure          = exposureState;
        triangles         = triangleLights;
        imageBytes        = vk::DeviceSize(extent.width) * extent.height * 4;
        buffer            = graph.write(graph.createBuffer({
                       .size             = imageBytes + sizeof(ExposurePass::State),
                       .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        }));
        graph.use(
            image,
            {
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
                .layout = vk::ImageLayout::eTransferSrcOptimal,
            }
        );
        graph.use(
            buffer,
            {
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            }
        );
        graph.use(
            exposure,
            {
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
            }
        );
        if (triangles.id != RenderGraph::kInvalidHandle)
        {
            graph.use(
                triangles,
                {
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                }
            );
        }
        return buffer;
    }

    void prepare(RenderGraph& graph, const PassFrame&)
    {
        triangleBytes = triangles.id == RenderGraph::kInvalidHandle ? 0 : graph.getBuffer(triangles).size();
        graph.ensureBufferCapacity(buffer, imageBytes + sizeof(ExposurePass::State) + triangleBytes);
    }

    void execute(RenderGraph& graph, vk::raii::CommandBuffer& commands, const PassFrame& frame) const
    {
        const vk::BufferImageCopy copy{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent      = graph.imageInfo(image).extent,
        };
        commands.copyImageToBuffer(
            graph.getImage(image).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer).handle(),
            copy
        );
        commands.copyBuffer(
            graph.getBuffer(exposure).handle(),
            graph.getBuffer(buffer).handle(),
            vk::BufferCopy{.dstOffset = imageBytes, .size = sizeof(ExposurePass::State)}
        );
        if (triangleBytes > 0)
        {
            commands.copyBuffer(
                graph.getBuffer(triangles).handle(),
                graph.getBuffer(buffer).handle(),
                vk::BufferCopy{.dstOffset = imageBytes + sizeof(ExposurePass::State), .size = triangleBytes}
            );
        }
    }
};

void capture(Renderer& renderer, SceneManager& scenes, const std::filesystem::path& output, uint32_t frameCount)
{
    auto&       vulkan                 = renderer.vulkanContext();
    const auto& swapchain              = renderer.swapchainHandle();
    const auto  extent                 = swapchain.extent();
    const auto  format                 = swapchain.surfaceFormat().format;
    const auto& config                 = context().config->rendererConfig();
    const bool  offline   = config.renderPath == RenderPath::OfflinePathTracing;
    const bool  realtime  = config.renderPath == RenderPath::RayTracing;
    const bool  lightMarkersAndPicking = context().config->editorConfig().lightMarkersAndPicking;

    std::array<FrameContext, maxFramesInFlight> frames;
    for (auto& frame : frames)
        frame.init(vulkan);
    std::array<FrameData, maxFramesInFlight> frameData;
    for (auto& data : frameData)
        data.init(vulkan, *renderContext().device);
    const PassSetup setup{vulkan, renderer.swapchainHandle().extent(), frameData, context().config->rendererConfig()};
    RenderGraph     graph;
    graph.init(vulkan);
    renderContext().materialTable->setup(graph);
    ShadowPass               shadow;
    DepthPrepass             depth;
    ScenePass                scene;
    GBufferPass              gBuffer;
    DeferredLightingPass     deferredLighting;
    SkyboxPass               skybox;
    TransparentPass          transparent;
    BlasBuildPass            blas;
    TlasBuildPass            tlas;
    OfflinePathTracingPass   tracing;
    RayTracingPass           realtimeTracing;
    RayTracingScenePass      realtimeScene;
    SVGFReprojection         reprojection;
    SVGFFilter               filter;
    Bloom                    bloom;
    ToneMappingPass          toneMapping;
    ExposurePass             exposure;
    EditorDepthPass          editorDepth;
    LightMarkerPass          lightMarker;
    ImageReadbackPass        readback;
    EndPass                  end;

    const auto               target = graph.createImage({.format = format, .extent = {extent.width, extent.height, 1}});
    RenderGraph::ImageHandle color;
    RenderGraph::ImageHandle depthImage;
    RenderGraph::BufferHandle triangleLights;
    ShadowPass::Output       lightingShadow;
    const auto               lightBuffer = graph.createBuffer({
                      .size  = kMaxLights * sizeof(LightData),
                      .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    if (offline || realtime)
    {
        const auto meshes     = graph.addPass(blas, setup);
        const auto structures = graph.addPass(tlas, setup, meshes);
        if (offline)
            color = graph.addPass(tracing, setup, structures).rayImage;
        else
        {
            const auto geometry = graph.addPass(realtimeScene, setup);
            triangleLights = geometry.triangles;
            const auto traced = graph.addPass(realtimeTracing, setup, structures, geometry, lightBuffer);
            if (config.feature.rayTracing.svgf)
            {
                const auto accumulated = graph.addPass(reprojection, setup, traced);
                color = filter.setup(graph, setup, traced, accumulated, reprojection).color;
            }
            else
                color = traced.rayImage;
        }
    }
    else
    {
        ShadowPass::Output shadows;
        if (config.feature.shadow.enable)
            shadows = graph.addPass(shadow, setup, lightBuffer);
        const auto depthOutput = graph.addPass(depth, setup);
        if (config.renderPath == RenderPath::Deferred)
        {
            const auto geometry = graph.addPass(gBuffer, setup, depthOutput.depth);
            const auto lit      = graph.addPass(deferredLighting, setup, target, geometry, shadows, lightBuffer);
            color               = lit.color;
            depthImage          = lit.depth;
            lightingShadow      = lit.shadow;
        }
        else
        {
            const auto lit = graph.addPass(scene, setup, target, depthOutput.depth, shadows, lightBuffer);
            color          = lit.color;
            depthImage     = lit.depth;
            lightingShadow = lit.shadow;
        }
        if (config.feature.ibl)
            color = graph.addPass(skybox, setup, color, depthImage).color;
        color = graph.addPass(transparent, setup, color, depthImage, lightingShadow, lightBuffer).color;
    }
    const auto exposureOutput = graph.addPass(exposure, setup, color);
    if (!offline && lightMarkersAndPicking)
    {
        if (depthImage.id == RenderGraph::kInvalidHandle)
            depthImage = graph.addPass(editorDepth, setup).depth;
        color = graph.addPass(lightMarker, setup, color, depthImage).color;
    }
    if (config.feature.bloom.enable)
        color = bloom.setup(graph, setup, color).color;
    color             = graph.addPass(toneMapping, setup, color, target, exposureOutput.state).color;
    const auto buffer = graph.addPass(readback, setup, color, exposureOutput.state, triangleLights);
    graph.addPass(end, setup, RenderGraph::ImageHandle{}, buffer);
    graph.compile(setup);
    if (config.renderPath == RenderPath::Deferred)
        deferredLighting.writeDescriptorSet(graph, setup);
    bindTestLightBuffer(graph, setup, lightBuffer);

    EditorCamera camera{scenes.scene().editorCamera};
    const auto   view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
    RenderSystem system;
    const auto&  snapshot = system.prepare(scenes.scene());
    graph.beginFrame(0);
    const auto&  renderData = renderer.prepare(view, snapshot, 0);
    LightMarkers lightMarkers;
    if (!offline && lightMarkersAndPicking)
        lightMarkers.prepare(scenes.scene(), *renderContext().resources);
    const EditorFrameInput editor{.lightDrawItems = lightMarkers.drawItems()};
    vk::raii::Sampler      lightingSampler = nullptr;
    if (offline || realtime)
    {
        writeTestEnvironment(setup, renderData.environmentMap);
        for (auto& data : frameData)
            data.uploadSceneUniforms(view.uniforms);
    }
    else
    {
        lightingSampler = writeTestLighting(graph, setup, lightingShadow, renderData);
        for (auto& data : frameData)
        {
            data.uploadSceneUniforms(view.uniforms);
        }
    }

    const auto activeFrameCount = offline ? 1u : maxFramesInFlight;
    for (uint32_t submitted = 0, frameNumber = 0; frameNumber < frameCount; ++submitted)
    {
        const auto index = submitted % activeFrameCount;
        auto&      frame = frames[index];
        frame.wait();
        if (submitted != 0)
            graph.beginFrame(index);
        uploadTestLights(graph, lightBuffer, renderData.lights);
        const PassFrame execution{setup, frameData[index], view, renderData, editor, index, 1.0f / 60.0f};
        graph.prepare(execution);
        auto& commands = frame.commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        graph.execute(commands, execution, frame.gpuProfiler(), nullptr);
        vkCheck(commands.end());
        const auto command = *commands;
        vkCheck(vulkan.deviceHandle().resetFences(frame.drawFenceHandle()));
        vkCheck(vulkan.queueHandle().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command},
            frame.drawFenceHandle()
        ));
        const auto completed = offline ? tracing.sampleCount() : frameNumber + 1;
        if (completed != frameNumber)
        {
            frameNumber = completed;
            if (frameNumber % 64 == 0 || frameNumber == frameCount)
                LOG_INFO("{}: sample {}/{}", scenes.scene().id, frameNumber, frameCount);
        }
    }
    for (auto& frame : frames)
        frame.wait();

    std::error_code error;
    if (!output.parent_path().empty())
        std::filesystem::create_directories(output.parent_path(), error);
    CHECK(!error, "cannot create output directory '{}': {}", output.string(), error.message());

    const auto           imageBytes = size_t(extent.width) * extent.height * 4;
    std::vector<uint8_t> pixels(imageBytes + sizeof(ExposurePass::State) + readback.triangleBytes);
    graph.getBuffer(buffer).download(pixels.data(), pixels.size());
    ExposurePass::State exposureState;
    std::memcpy(&exposureState, pixels.data() + imageBytes, sizeof(exposureState));
    CHECK(
        std::isfinite(exposureState.ev100) && std::isfinite(exposureState.exposureScale) &&
        exposureState.exposureScale > 0.0f
    );
    LOG_INFO(
        "Exposure: EV100={}, scale={}, automaticValid={}",
        exposureState.ev100,
        exposureState.exposureScale,
        exposureState.automaticValid
    );
    if (realtime)
    {
        const auto             offset = imageBytes + sizeof(ExposurePass::State);
        EmissiveTriangleHeader header;
        std::memcpy(&header, pixels.data() + offset, sizeof(header));
        CHECK(header.triangleCount == realtimeScene.triangleCount());
        auto path = output;
        path.replace_extension(".lights.bin");
        std::ofstream stream(path, std::ios::binary);
        CHECK(stream, "cannot write triangle light capture: {}", path.string());
        stream.write(
            reinterpret_cast<const char*>(pixels.data() + offset),
            sizeof(header) + size_t(header.triangleCount) * sizeof(EmissiveTriangle)
        );
        CHECK(stream);
        LOG_INFO("Emissive triangle lights: {}", header.triangleCount);
    }
    pixels.resize(imageBytes);
    if (format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eB8G8R8A8Unorm)
        for (size_t offset = 0; offset < pixels.size(); offset += 4)
            std::swap(pixels[offset], pixels[offset + 2]);
    CHECK(
        stbi_write_png(
            output.string().c_str(),
            int(extent.width),
            int(extent.height),
            4,
            pixels.data(),
            int(extent.width * 4)
        ),
        "cannot write capture: {}",
        output.string()
    );
    LOG_INFO("Saved {} ({}x{}, {} frames)", output.string(), extent.width, extent.height, frameCount);

    graph.reset();
    for (auto& data : frameData)
        data.reset();
    for (auto& frame : frames)
        frame.reset();
}
}  // namespace

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(
        argc >= 4 && argc <= 7,
        "usage: CaptureScene <scene-id> <forward|deferred|realtime|offline> <output.png> [frames=1] [size=960] [ev100|auto]"
    );
    const std::string_view route{argv[2]};
    CHECK(
        route == "forward" || route == "deferred" || route == "realtime" || route == "offline",
        "unsupported capture route: {}",
        route
    );
    const auto frameCount = argc > 4 ? parseNumber<uint32_t>(argv[4]) : 1u;
    const auto side       = argc > 5 ? parseNumber<int>(argv[5]) : 960;
    CHECK(frameCount > 0 && side > 0);

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
    const auto originalConfig = config.rendererConfig();
    const auto originalEditor = config.editorConfig();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    scenes.load(SceneAsset::ID{argv[1]});
    auto feature = originalConfig.feature;
    feature.hdr  = true;
    feature.ibl  = scenes.scene().environmentMap.has_value();
    if (argc > 6)
    {
        auto& exposure = scenes.scene().editorCamera.exposure;
        exposure = exposure.value_or(feature.toneMapping.exposure);
        if (std::string_view{argv[6]} == "auto")
        {
            exposure->mode = ExposureMode::Automatic;
        }
        else
        {
            exposure->mode = ExposureMode::Manual;
            exposure->ev100 = parseNumber<float>(argv[6]);
        }
        CHECK(exposure->isValid());
    }
    config.setRenderFeature(feature);
    CHECK((route == "offline") == (scenes.scene().renderMode == SceneRenderMode::Offline),
          "capture route does not match scene render mode");
    config.setSceneRenderMode(scenes.scene().renderMode);
    if (route != "offline")
        config.setRenderPath(
            route == "realtime"       ? RenderPath::RayTracing
                : route == "deferred" ? RenderPath::Deferred
                                      : RenderPath::Forward
        );
    config.setEditorSize(side, side);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    renderer.rebuildGraph();
    capture(renderer, scenes, std::filesystem::path{argv[3]}, frameCount);
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setSceneRenderMode(originalConfig.renderPath == RenderPath::OfflinePathTracing ? SceneRenderMode::Offline : SceneRenderMode::Realtime);
    if (originalConfig.renderPath != RenderPath::OfflinePathTracing)
        config.setRenderPath(originalConfig.renderPath);
    config.setRenderFeature(originalConfig.feature);
    config.setEditorSize(originalEditor.width, originalEditor.height);
    config.shutdown();
    context() = {};
}
