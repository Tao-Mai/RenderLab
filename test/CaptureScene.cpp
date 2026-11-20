#include "GraphSelection.h"
#include "LightingSetup.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/graph/PassPorts.h"
#include "render/postprocess/ExposureState.h"
#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"
#include "render/device/VkCheck.h"
#include "render/graph/EndPass.h"
#include "render/raytracing/SceneAccelerationStructures.h"
#include "render/resources/MaterialTable.h"
#include "render/editor/EditorFrameInput.h"
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
    RenderGraph::ImageHandle  hdrImage;
    RenderGraph::BufferHandle buffer;
    RenderGraph::BufferHandle exposure;
    RenderGraph::BufferHandle triangles;
    RenderGraph::BufferHandle reservoirs;
    vk::DeviceSize            imageBytes;
    vk::DeviceSize            exposureBytes = 0;
    vk::DeviceSize            hdrBytes;
    vk::DeviceSize            triangleBytes = 0;
    vk::DeviceSize            reservoirBytes = 0;

    RenderGraph::BufferHandle compile(
        RenderGraph& graph,
        const PassSetup&,
        RenderGraph::ImageHandle  source,
        RenderGraph::ImageHandle  sourceHdr,
        RenderGraph::BufferHandle exposureState,
        RenderGraph::BufferHandle triangleLights,
        RenderGraph::BufferHandle reservoirHistory
    )
    {
        image             = source;
        hdrImage          = sourceHdr;
        const auto extent = graph.imageInfo(image).extent;
        exposure          = exposureState;
        exposureBytes     = exposure.id == RenderGraph::kInvalidHandle ? 0 : sizeof(ExposureState);
        triangles         = triangleLights;
        reservoirs        = reservoirHistory;
        imageBytes        = vk::DeviceSize(extent.width) * extent.height * 4;
        CHECK(graph.imageInfo(hdrImage).format == vk::Format::eR16G16B16A16Sfloat);
        hdrBytes          = vk::DeviceSize(extent.width) * extent.height * 8;
        buffer            = graph.write(graph.createBuffer({
                       .size             = imageBytes + exposureBytes,
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
            hdrImage,
            {
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferRead,
                .layout = vk::ImageLayout::eTransferSrcOptimal,
            }
        );
        if (exposureBytes > 0)
            graph.use(
                exposure,
                {
                    .stages = vk::PipelineStageFlagBits2::eTransfer,
                    .access = vk::AccessFlagBits2::eTransferRead,
                }
            );
        for (const auto source : {triangles, reservoirs})
        {
            if (source.id == RenderGraph::kInvalidHandle)
            {
                continue;
            }
            graph.use(
                source,
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
        reservoirBytes = reservoirs.id == RenderGraph::kInvalidHandle ? 0 : graph.getBuffer(reservoirs).size();
        graph.ensureBufferCapacity(
            buffer,
            imageBytes + exposureBytes + triangleBytes + reservoirBytes + hdrBytes
        );
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
        auto hdrCopy         = copy;
        hdrCopy.bufferOffset = imageBytes + exposureBytes + triangleBytes + reservoirBytes;
        commands.copyImageToBuffer(
            graph.getImage(hdrImage).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer).handle(),
            hdrCopy
        );
        if (exposureBytes > 0)
            commands.copyBuffer(
                graph.getBuffer(exposure).handle(),
                graph.getBuffer(buffer).handle(),
                vk::BufferCopy{.dstOffset = imageBytes, .size = exposureBytes}
            );
        if (triangleBytes > 0)
        {
            commands.copyBuffer(
                graph.getBuffer(triangles).handle(),
                graph.getBuffer(buffer).handle(),
                vk::BufferCopy{.dstOffset = imageBytes + exposureBytes, .size = triangleBytes}
            );
        }
        if (reservoirBytes > 0)
        {
            commands.copyBuffer(
                graph.getBuffer(reservoirs).handle(),
                graph.getBuffer(buffer).handle(),
                vk::BufferCopy{
                    .dstOffset = imageBytes + exposureBytes + triangleBytes,
                    .size      = reservoirBytes
                }
            );
        }
    }
};

void capture(
    Renderer&                    renderer,
    SceneManager&                scenes,
    const std::filesystem::path& output,
    uint32_t                     frameCount,
    const std::filesystem::path& sequenceDirectory,
    float                        yawStep
)
{
    auto&       vulkan                 = renderer.vulkanContext();
    const auto& swapchain              = renderer.swapchainHandle();
    const auto  extent                 = swapchain.extent();
    const auto  format                 = swapchain.surfaceFormat().format;
    const auto& config                 = context().config->rendererConfig();
    const bool  offline   = config.renderPath == RenderPath::OfflinePathTracing;
    const bool  realtime  = config.renderPath == RenderPath::RayTracing;

    std::array<FrameContext, maxFramesInFlight> frames;
    for (auto& frame : frames)
        frame.init(vulkan);
    std::array<FrameData, maxFramesInFlight> frameData;
    for (auto& data : frameData)
        data.init(vulkan, *renderContext().device);
    const PassSetup setup{vulkan, renderer.swapchainHandle().extent(), frameData, context().config->rendererConfig()};
    RenderGraph graph;
    SceneAccelerationStructures accelerationStructures;
    graph.init(vulkan);
    renderContext().materialTable->setup(graph);
    RenderGraphCompiler captureGraph;
    ImageReadbackPass readback;
    EndPass end;

    const auto target = graph.createImage({.format = format, .extent = {extent.width, extent.height, 1}});
    const auto lightBuffer = graph.createBuffer({
        .size = kMaxLights * sizeof(LightData),
        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    });
    const auto& configManager = *context().config;
    auto description = loadRenderGraph(configManager.renderGraphScript(), configManager.graphParameterOverrides());
    const auto sourceOf = [&](std::string_view input) -> const nlohmann::json&
    {
        const auto& edges = description.at("edges");
        const auto edge = std::ranges::find_if(edges, [&](const auto& value) { return value[1] == input; });
        CHECK(edge != edges.end(), "capture graph is missing '{}'", input);
        return (*edge)[0];
    };

    auto& marked = description["outputs"];
    marked["color"] = sourceOf("editorUi.color");
    marked.erase("pickingReadback");
    marked["hdr"] = sourceOf(config.feature.postProcess == PostProcessMode::Falcor ? "falcor.source"
                                                                                 : "exposure.hdrColor");
    if (config.feature.postProcess == PostProcessMode::RenderLab)
        marked["exposure"] = "exposure.state";
    if (realtime)
    {
        marked["triangleLights"] = "rayTracingScene.triangles";
        const auto* trace = renderer.graphPasses().findPass("rayTracing");
        CHECK(trace);
        if (trace->pass->properties().at("algorithm") == "ReSTIR")
            marked["reservoirs"] = "rayTracing.reservoirHistory";
    }
    GraphResources inputs{{"presentTarget", target}, {"lightBuffer", lightBuffer}};
    if (graphUsesInputPrefix(description, "emptyShadow_"))
        pass_ports::insert(inputs, "emptyShadow_", createUnshadowedResources(graph));
    const auto outputs = captureGraph.compile(graph, setup, description, inputs, renderer.plugins(),
                                              configManager.passOverrides());
    const auto optionalBuffer = [&](std::string_view name)
    {
        const auto found = outputs.find(name);
        return found == outputs.end() ? RenderGraph::BufferHandle{} : std::get<RenderGraph::BufferHandle>(found->second);
    };
    const auto buffer = graph.addStage(
        "Capture readback", readback, setup,
        std::get<RenderGraph::ImageHandle>(outputs.at("color")),
        std::get<RenderGraph::ImageHandle>(outputs.at("hdr")),
        optionalBuffer("exposure"), optionalBuffer("triangleLights"), optionalBuffer("reservoirs")
    );
    graph.addStage("Capture host read", end, setup, RenderGraph::ImageHandle{}, buffer);
    graph.compile(setup);
    bindTestLightBuffer(graph, setup, lightBuffer);

    EditorCamera camera{scenes.scene().editorCamera};
    auto         view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
    RenderSystem system;
    const auto&  snapshot = system.prepare(scenes.scene());
    graph.beginFrame(0);
    const auto&  renderData = renderer.prepare(view, snapshot, 0);
    const EditorFrameInput editor;
    if (offline || realtime)
    {
        writeTestEnvironment(setup, renderData.environmentMap);
        for (auto& data : frameData)
            data.uploadSceneUniforms(view.uniforms);
    }
    else
    {
        writeTestLighting(graph, setup, pass_ports::read<ShadowResources>(outputs), renderData);
        for (auto& data : frameData)
        {
            data.uploadSceneUniforms(view.uniforms);
        }
    }

    const auto activeFrameCount = offline ? 1u : maxFramesInFlight;
    if (!sequenceDirectory.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(sequenceDirectory, error);
        CHECK(!error, "cannot create sequence directory '{}': {}", sequenceDirectory.string(), error.message());
    }
    for (uint32_t submitted = 0, frameNumber = 0; frameNumber < frameCount; ++submitted)
    {
        const auto index = submitted % activeFrameCount;
        auto&      frame = frames[index];
        frame.wait();
        if (yawStep != 0.0f)
        {
            camera.config().yaw += yawStep;
            view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
            frameData[index].uploadSceneUniforms(view.uniforms);
        }
        if (submitted != 0)
            graph.beginFrame(index);
        uploadTestLights(graph, lightBuffer, renderData.lights);
        PassFrame execution{setup, frameData[index], view, renderData, editor, index, 1.0f / 60.0f};
        graph.prepare(execution);
        auto& commands = frame.commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        auto& gpuProfiler = frame.gpuProfiler();
        graph.beginGpuProfiling(commands, gpuProfiler, nullptr);
        if (offline || realtime)
        {
            execution.sceneTlas = &accelerationStructures.recordBuilds(
                vulkan, commands, *renderContext().resources, renderData, index
            );
        }
        graph.execute(commands, execution, gpuProfiler, nullptr);
        gpuProfiler.endFrame(commands);
        vkCheck(commands.end());
        const auto command = *commands;
        vkCheck(vulkan.deviceHandle().resetFences(frame.drawFenceHandle()));
        vkCheck(vulkan.queueHandle().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command},
            frame.drawFenceHandle()
        ));
        if (!sequenceDirectory.empty())
        {
            frame.wait();
            std::vector<uint8_t> pixels(
                readback.imageBytes + readback.exposureBytes + readback.triangleBytes + readback.reservoirBytes +
                readback.hdrBytes
            );
            graph.getBuffer(buffer).download(pixels.data(), pixels.size());
            const auto    hdrPath = sequenceDirectory / (std::to_string(submitted) + ".hdr.bin");
            std::ofstream hdrStream(hdrPath, std::ios::binary);
            CHECK(hdrStream);
            hdrStream.write(
                reinterpret_cast<const char*>(pixels.data() + pixels.size() - readback.hdrBytes),
                readback.hdrBytes
            );
            CHECK(hdrStream);
            pixels.resize(size_t(extent.width) * extent.height * 4);
            if (format == vk::Format::eB8G8R8A8Srgb || format == vk::Format::eB8G8R8A8Unorm)
            {
                for (size_t offset = 0; offset < pixels.size(); offset += 4)
                {
                    std::swap(pixels[offset], pixels[offset + 2]);
                }
            }
            const auto path = sequenceDirectory / (std::to_string(submitted) + ".png");
            CHECK(stbi_write_png(
                path.string().c_str(),
                int(extent.width),
                int(extent.height),
                4,
                pixels.data(),
                int(extent.width * 4)
            ));
        }
        const auto completed = offline
            ? captureGraph.findPass("offline")->pass->statistics().at("sampleCount").get<uint32_t>()
            : frameNumber + 1;
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
    std::vector<uint8_t> pixels(
        imageBytes + readback.exposureBytes + readback.triangleBytes + readback.reservoirBytes
    );
    graph.getBuffer(buffer).download(pixels.data(), pixels.size());
    if (readback.exposureBytes > 0)
    {
        ExposureState exposureState;
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
    }
    if (realtime)
    {
        const auto             offset = imageBytes + readback.exposureBytes;
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
        if (readback.reservoirBytes > 0)
        {
            path = output;
            path.replace_extension(".restir.bin");
            std::ofstream reservoirStream(path, std::ios::binary);
            CHECK(reservoirStream);
            reservoirStream.write(
                reinterpret_cast<const char*>(pixels.data() + offset + readback.triangleBytes),
                readback.reservoirBytes
            );
            CHECK(reservoirStream);
        }
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
        argc >= 4 && argc <= 12,
        "usage: CaptureScene <scene-id> <forward|deferred|realtime|offline> <output.png> [frames=1] [size=960] "
        "[ev100|auto] [sequence-directory] [yaw-step=0] [renderlab|falcor] [restir|minimalpathtracer] [max-bounces]"
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
    const auto originalGraph = config.renderGraph().id;
    const auto originalEditor = config.editorConfig();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    scenes.load(SceneAsset::ID{argv[1]});
    CHECK((route == "offline") == (scenes.scene().renderMode == SceneRenderMode::Offline),
          "capture route does not match scene render mode");
    config.setSceneRenderMode(scenes.scene().renderMode);
    if (route != "offline")
        selectGraphForPath(
            config,
            route == "realtime"       ? RenderPath::RayTracing
                : route == "deferred" ? RenderPath::Deferred
                                      : RenderPath::Forward
        );
    const auto originalParameters = config.graphParameterOverrides();
    const auto originalOverrides = config.passOverrides();

    auto parameters = originalParameters;
    parameters["hdr"] = true;
    parameters["ibl"] = scenes.scene().environmentMap.has_value();
    if (argc > 9)
    {
        const std::string_view postProcess{argv[9]};
        CHECK(postProcess == "renderlab" || postProcess == "falcor");
        parameters["postProcess"] =
            Serialize(postProcess == "falcor" ? PostProcessMode::Falcor : PostProcessMode::RenderLab);
    }
    config.setGraphParameterOverrides(std::move(parameters));
    config.setEditorSize(side, side);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    renderer.rebuildGraph();
    const auto mode = config.rendererConfig().feature.postProcess;

    if (argc > 10)
    {
        CHECK(route == "realtime");
        const std::string_view algorithm{argv[10]};
        CHECK(algorithm == "restir" || algorithm == "minimalpathtracer");
        auto settings = renderer.graphPasses().findPass("rayTracing")->pass->properties();
        settings["algorithm"] = algorithm == "minimalpathtracer" ? "MinimalPathTracer" : "ReSTIR";
        if (argc > 11)
            settings["maxBounces"] = parseNumber<uint32_t>(argv[11]);
        renderer.setPassOverrides("RayTracingPass", "rayTracing", std::move(settings));
    }
    if (argc > 6 && mode == PostProcessMode::Falcor)
    {
        FalcorPostProcessConfig settings;
        Deserialize(renderer.graphPasses().findPass("falcor")->pass->properties(), settings);
        settings.toneMapper.autoExposure = std::string_view{argv[6]} == "auto";
        if (!settings.toneMapper.autoExposure)
            settings.toneMapper.setExposureValue(parseNumber<float>(argv[6]));
        renderer.setPassOverrides("FalcorPostProcess", "falcor", Serialize(settings));
    }
    else if (argc > 6)
    {
        auto& exposure = scenes.scene().editorCamera.exposure;
        exposure = exposure.value_or(renderer.defaultExposure());
        if (std::string_view{argv[6]} == "auto")
            exposure->mode = ExposureMode::Automatic;
        else
        {
            exposure->mode = ExposureMode::Manual;
            exposure->ev100 = parseNumber<float>(argv[6]);
        }
        CHECK(exposure->isValid());
    }

    if (argc > 10 || (argc > 6 && mode == PostProcessMode::Falcor))
        renderer.rebuildGraph();

    capture(
        renderer,
        scenes,
        std::filesystem::path{argv[3]},
        frameCount,
        argc > 7 ? std::filesystem::path{argv[7]} : std::filesystem::path{},
        argc > 8 ? parseNumber<float>(argv[8]) : 0.0f
    );
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setGraphParameterOverrides(originalParameters);
    config.passOverrides() = originalOverrides;
    config.setSceneRenderMode(SceneRenderMode::Realtime);
    config.setRenderGraph(originalGraph);
    config.setEditorSize(originalEditor.width, originalEditor.height);
    config.shutdown();
    context() = {};
}
