#include "render/device/ResourceBarriers.h"
#include "LightingSetup.h"
#include "PathTracingStats.h"
#include "RendererOptions.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Scripting.h"
#include "core/Serializer.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/graph/RenderGraphImportExport.h"
#include "render/postprocess/ExposureState.h"
#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"
#include "render/device/VkCheck.h"
#include "render/raytracing/RealtimeSceneData.h"
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

struct ImageReadbackPass : Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ImageReadbackPass, "ImageReadbackPass", "Copies a captured graph output.");

    explicit ImageReadbackPass(Falcor::ref<Falcor::Device> device) : Falcor::RenderPass(std::move(device)) {}

    ResourceBarriers barriers;
    VulkanResources::ImageHandle  image;
    VulkanResources::ImageHandle  hdrImage;
    VulkanResources::BufferHandle buffer;
    VulkanResources::BufferHandle exposure;
    VulkanResources::BufferHandle triangles;
    VulkanResources::BufferHandle reservoirs;
    vk::DeviceSize            imageBytes;
    vk::DeviceSize            exposureBytes = 0;
    vk::DeviceSize            hdrBytes;
    vk::DeviceSize            triangleBytes = 0;
    vk::DeviceSize            reservoirBytes = 0;
    ResourceBarriers        copyAccesses;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        reflection.addInput("color", "Color target");
        reflection.addInput("hdr", "Linear HDR radiance").format(Falcor::ResourceFormat::RGBA16Float);
        for (const auto name : {"exposure", "triangleLights", "reservoirs"})
            reflection.addInput(name, "Optional capture data").rawBuffer(0).flags(Falcor::RenderPassReflection::Field::Flags::Optional);
        reflection.addInputOutput("capture", "Host-visible capture buffer").rawBuffer(0);
        return reflection;
    }

    void compile(Falcor::RenderContext*, const CompileData&) override
    {
        copyAccesses.clear();
    }

    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override
    {
        auto& cache    = *renderCommands->vulkanResources;
        auto& commands = *renderCommands->commands;

        if (copyAccesses.empty())
        {
            image    = cache.registerImage(renderData.getTexture("color"));
            hdrImage = cache.registerImage(renderData.getTexture("hdr"));
            buffer   = cache.registerBuffer(renderData["capture"]->asBuffer());
            copyAccesses.add(buffer, vulkan_access::kTransferWrite);
            copyAccesses.declare(cache);
            const auto optionalBuffer = [&](std::string_view name)
            {
                const auto& resource = renderData[name];
                return resource ? cache.registerBuffer(resource->asBuffer()) : VulkanResources::BufferHandle{};
            };
            exposure   = optionalBuffer("exposure");
            triangles  = optionalBuffer("triangleLights");
            reservoirs = optionalBuffer("reservoirs");

            const auto extent    = cache.imageInfo(image).extent;
            imageBytes           = vk::DeviceSize(extent.width) * extent.height * 4;
            const auto hdrExtent = cache.imageInfo(hdrImage).extent;
            hdrBytes             = vk::DeviceSize(hdrExtent.width) * hdrExtent.height * 8;
            exposureBytes        = exposure.id == VulkanResources::kInvalidHandle ? 0 : sizeof(ExposureState);

            barriers.add(image, vulkan_access::kTransferSource);
            barriers.add(hdrImage, vulkan_access::kTransferSource);
            constexpr VulkanResources::MemoryAccess kRead{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead};
            for (const auto input : {exposure, triangles, reservoirs})
                if (input.id != VulkanResources::kInvalidHandle)
                    barriers.add(input, kRead);
            barriers.declare(cache);
        }

        barriers.transition(cache, commands);

        triangleBytes  = triangles.id == VulkanResources::kInvalidHandle ? 0 : cache.getBuffer(triangles).getSize();
        reservoirBytes = reservoirs.id == VulkanResources::kInvalidHandle ? 0 : cache.getBuffer(reservoirs).getSize();
        cache.ensureBufferCapacity(buffer, imageBytes + exposureBytes + triangleBytes + reservoirBytes + hdrBytes);
        copyAccesses.transition(cache, commands);

        const vk::BufferImageCopy copy{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageExtent      = cache.imageInfo(image).extent,
        };
        commands.copyImageToBuffer(
            cache.getImage(image).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            cache.getBuffer(buffer).handle(),
            copy
        );
        auto hdrCopy         = copy;
        hdrCopy.imageExtent  = cache.imageInfo(hdrImage).extent;
        hdrCopy.bufferOffset = imageBytes + exposureBytes + triangleBytes + reservoirBytes;
        commands.copyImageToBuffer(
            cache.getImage(hdrImage).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            cache.getBuffer(buffer).handle(),
            hdrCopy
        );
        if (exposureBytes > 0)
            commands.copyBuffer(
                cache.getBuffer(exposure).handle(),
                cache.getBuffer(buffer).handle(),
                vk::BufferCopy{.dstOffset = imageBytes, .size = exposureBytes}
            );
        if (triangleBytes > 0)
        {
            commands.copyBuffer(
                cache.getBuffer(triangles).handle(),
                cache.getBuffer(buffer).handle(),
                vk::BufferCopy{.dstOffset = imageBytes + exposureBytes, .size = triangleBytes}
            );
        }
        if (reservoirBytes > 0)
        {
            commands.copyBuffer(
                cache.getBuffer(reservoirs).handle(),
                cache.getBuffer(buffer).handle(),
                vk::BufferCopy{.dstOffset = imageBytes + exposureBytes + triangleBytes, .size = reservoirBytes}
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
    const Falcor::ref<Falcor::RenderGraph> active{renderer.getActiveGraph()};
    const auto device = active->getDevice();
    const auto& deviceContext = *device->getRenderContext();
    const auto traceInstance = active->doesPassExist("rayTracing") ? "rayTracing" : "pathTracer";
    const auto progressKey = path_tracing_test::statsKey(active->doesPassExist("pt") ? "pt" : "bdpt");

    std::array<FrameContext, maxFramesInFlight> frames;
    for (auto& frame : frames)
        frame.init(vulkan);
    std::array<FrameData, maxFramesInFlight> frameData;
    for (auto& data : frameData)
        data.init(vulkan, *device);
    SceneAccelerationStructures accelerationStructures;

    const auto graphIR = Falcor::RenderGraphExporter::getIR(active);
    const auto functionName = Falcor::RenderGraphExporter::getFuncName(active->getName());
    auto graph = Falcor::RenderGraph::createFromString(
        device, graphIR + "\nm.addGraph(" + functionName + "())\n"
    );
    CHECK(graph);
    const auto color = graph->getOutputName(0);

    Falcor::Scripting::Context script;
    script.setObject("graphIR", graphIR);
    script.setObject("hdrInput", std::string{
        config.feature.postProcess == PostProcessMode::Falcor ? "falcor.source" : "exposure.hdrColor"
    });
    Falcor::Scripting::runScript(R"(
import ast
hdrOutput = next(
    ast.literal_eval(call.args[0])
    for call in ast.walk(ast.parse(graphIR))
    if isinstance(call, ast.Call) and isinstance(call.func, ast.Attribute)
    and call.func.attr == 'add_edge' and ast.literal_eval(call.args[1]) == hdrInput
)
)", script);
    const auto hdr = script.getObject<std::string>("hdrOutput");

    graph->setScene(scenes.getScene());
    while (graph->getOutputCount() != 0)
        graph->unmarkOutput(graph->getOutputName(graph->getOutputCount() - 1));

    const auto lights = createTestLightBuffer(*device);
    const auto makeReadback = [&](vk::DeviceSize bytes)
    {
        return device->createBuffer(bytes, Falcor::ResourceBindFlags::None, Falcor::MemoryType::ReadBack);
    };
    const auto  readbackPass = Falcor::make_ref<ImageReadbackPass>(graph->getDevice());
    const auto& readback     = *readbackPass;
    graph->addPass(readbackPass, "capture");
    graph->setInput("capture.capture", makeReadback(vk::DeviceSize(extent.width) * extent.height * 4));
    graph->addEdge(color, "capture.color");
    graph->addEdge(hdr, "capture.hdr");
    if (config.feature.postProcess == PostProcessMode::RenderLab)
        graph->addEdge("exposure.state", "capture.exposure");
    if (realtime)
    {
        const auto trace = renderer.getActiveGraph()->getPass(traceInstance);
        DCHECK(trace);
        if (trace->getType() == "RayTracingPass")
        {
            graph->addEdge(std::string{trace->getName()} + ".triangleLights", "capture.triangleLights");
            graph->addEdge(std::string{trace->getName()} + ".reservoirHistory", "capture.reservoirs");
        }
    }
    graph->markOutput("capture.capture");

    graph->onResize(&swapchain.getFbo(0));
    VulkanResources cache{vulkan};
    auto compileContext = deviceContext;
    compileContext.vulkanResources = &cache;
    compileContext.frames = frameData;
    CHECK(graph->compile(&compileContext));
    const auto lightBuffer = cache.registerBuffer(lights);
    const auto buffer = graph->getOutput("capture.capture")->asBuffer();
    const auto readbackSlot = cache.registerBuffer(buffer);
    deviceContext.materialTable->setup(cache, vulkan);
    bindTestLightBuffer(cache, frameData, lightBuffer);

    EditorCamera camera{scenes.scene().editorCamera};
    auto         view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
    RenderSystem system;
    const auto&  snapshot = system.prepare(scenes.scene());
    cache.beginFrame(0);
    const auto&  renderData = renderer.prepare(view, snapshot, 0);
    const EditorFrameInput editor;
    if (offline || realtime)
    {
        writeTestEnvironment(*deviceContext.resources, frameData, renderData.environmentMap);
        for (auto& data : frameData)
            data.uploadSceneUniforms(view.uniforms);
    }
    else
    {
        writeTestLighting(*deviceContext.resources, frameData, config.feature, renderData);
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
            cache.beginFrame(index);
        uploadTestLights(cache, lightBuffer, renderData.lights);
        auto& commands = frame.commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        auto& gpuProfiler = frame.gpuProfiler();
        auto renderCommands = deviceContext;
        renderCommands.commands = &commands;
        renderCommands.vulkanResources = &cache;
        renderCommands.frames = frameData;
        renderCommands.frameData = &frameData[index];
        renderCommands.view = &view;
        renderCommands.scene = &renderData;
        renderCommands.editor = &editor;
        renderCommands.gpuProfiler = &gpuProfiler;
        renderCommands.frameIndex = index;
        renderCommands.deltaSeconds = 1.0f / 60.0f;
        if (offline || realtime)
        {
            renderCommands.sceneTlas = &accelerationStructures.recordBuilds(
                vulkan, commands, *renderCommands.resources, renderData, index
            );
        }
        cache.recordUploads(commands);
        graph->execute(&renderCommands);
        cache.transition(commands, readbackSlot, vulkan_access::kHostRead);
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
            buffer->getBlob(pixels.data(), 0, pixels.size());
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
            ? graph->getPassesDictionary().getValue<path_tracing_test::OfflineProgress>(progressKey).sampleCount
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
    buffer->getBlob(pixels.data(), 0, pixels.size());
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
    if (readback.triangleBytes > 0)
    {
        const auto             offset = imageBytes + readback.exposureBytes;
        EmissiveTriangleHeader header;
        std::memcpy(&header, pixels.data() + offset, sizeof(header));
        const auto triangleCount = graph->getPassesDictionary().getValue<uint32_t>(
            path_tracing_test::statsKey(traceInstance)
        );
        CHECK(header.triangleCount == triangleCount);
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

    graph = {};
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
        argc >= 4 && argc <= 11,
        "usage: CaptureScene <scene-id> <graph-name> "
        "<output.png> [frames=1] [size=960] [ev100|auto] [sequence-directory] [yaw-step=0] "
        "[renderlab|falcor] [max-bounces]"
    );
    const std::string_view graphName{argv[2]};
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
    const auto originalEditor = config.editorConfig();
    data.init(config.paths().assets);
    scenes.init();
    assets.init(config.paths().assets);
    scenes.load(SceneAsset::ID{argv[1]});
    config.setSceneRenderMode(scenes.scene().renderMode);
    const auto originalGraph = config.renderGraphName();
    config.setRenderGraph(graphName);
    const auto originalSettings = config.getSettings();

    auto parameters = config.getOptions().at("parameters");
    parameters["hdr"] = true;
    parameters["ibl"] = scenes.scene().environmentMap.has_value();
    if (argc > 9)
    {
        const std::string_view postProcess{argv[9]};
        CHECK(postProcess == "renderlab" || postProcess == "falcor");
        parameters["postProcess"] =
            Serialize(postProcess == "falcor" ? PostProcessMode::Falcor : PostProcessMode::RenderLab);
    }
    renderer_test::setParameters(renderer, config, parameters);
    config.setEditorSize(side, side);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();
    renderer.init();
    const auto& graph = *renderer.getActiveGraph();
    const auto mode = config.rendererConfig().feature.postProcess;

    if (argc > 10)
    {
        CHECK(config.rendererConfig().renderPath == RenderPath::RayTracing);
        const auto trace = graph.getPass(graph.doesPassExist("rayTracing") ? "rayTracing" : "pathTracer");
        CHECK(trace);
        auto settings          = nlohmann::json(trace->getProperties().toJson());
        settings["maxBounces"] = parseNumber<uint32_t>(argv[10]);
        trace->setProperties(Falcor::Properties::json(std::move(settings)));
    }
    if (argc > 6 && mode == PostProcessMode::Falcor)
    {
        auto& falcor = *graph.getPass("falcor");
        FalcorPostProcessConfig settings;
        Deserialize(nlohmann::json(falcor.getProperties().toJson()), settings);
        settings.toneMapper.autoExposure = std::string_view{argv[6]} == "auto";
        if (!settings.toneMapper.autoExposure)
            settings.toneMapper.setExposureValue(parseNumber<float>(argv[6]));
        falcor.setProperties(Falcor::Properties::json(Serialize(settings)));
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
    config.getSettings() = originalSettings;
    config.setRenderGraph(originalGraph);
    config.setEditorSize(originalEditor.width, originalEditor.height);
    config.shutdown();
    context() = {};
}
