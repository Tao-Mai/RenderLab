#include "render/device/ResourceBarriers.h"
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "GraphSelection.h"
#include "RendererOptions.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Scripting.h"
#include "core/Serializer.h"
#include "core/Window.h"
#include "editor/EditorCamera.h"
#include "render/FrameData.h"
#include "render/RenderContext.h"
#include "render/PreparedScene.h"
#include "render/RenderView.h"
#include "render/Renderer.h"
#include "render/device/FrameContext.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/editor/EditorFrameInput.h"
#include "render/graph/RenderPassHelpers.h"
#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"
#include "render/resources/RenderResourceManager.h"
#include "render/resources/NativeHandleTraits.h"
#include "render/resources/ResourceViews.h"
#include "scene/SceneManager.h"
#include "scene/system/SceneRenderData.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
constexpr vk::Extent3D kExtent{
    35, 19, 1
};  // Non-power-of-two, non-workgroup-aligned, and narrower than the bloom pyramid.
using Pixel = std::array<uint8_t, 4>;

struct SourcePass : Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(SourcePass, "SourcePass", "Provides pixels for post process validation.");

    explicit SourcePass(Falcor::ref<Falcor::Device> device, uint32_t samples = 1) :
        Falcor::RenderPass(std::move(device)), sampleCount(samples)
    {
    }

    ResourceBarriers barriers;
    VulkanResources::ImageHandle         color;
    uint32_t sampleCount;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        reflection.addOutput("color", "Color target")
            .format(Falcor::ResourceFormat::RGBA16Float)
            .texture2D(kExtent.width, kExtent.height, sampleCount);
        return reflection;
    }
    void compile(Falcor::RenderContext*, const CompileData&) override
    {
        barriers.clear();
    }
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override
    {
        auto& cache    = *renderCommands->vulkanResources;
        auto& commands = *renderCommands->commands;

        if (barriers.empty())
        {
            color = cache.registerImage(renderData.getTexture("color"));

            barriers.add(color, sampleCount > 1 ? vulkan_access::kColorWrite : vulkan_access::kTransferTarget);
            barriers.declare(cache);
        }

        barriers.transition(cache, commands);

        const vk::ClearColorValue clear{std::array{0.25f, 1.0f, 4.0f, 0.5f}};
        if (sampleCount > 1)
        {
            const vk::RenderingAttachmentInfo attachment{
                .imageView = cache.getImage(color).getOrCreateImageView({
                    .viewType = vk::ImageViewType::e2D,
                    .format = vk::Format::eR16G16B16A16Sfloat,
                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                }),
                .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                .loadOp = vk::AttachmentLoadOp::eClear,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .clearValue = clear,
            };
            commands.beginRendering({
                .renderArea = {{0, 0}, {kExtent.width, kExtent.height}},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &attachment,
            });
            commands.endRendering();
            return;
        }
        commands.clearColorImage(
            renderData.getTexture("color")->handle(),
            vk::ImageLayout::eTransferDstOptimal,
            clear,
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        );
    }
};
struct OverwritePass : Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(OverwritePass, "OverwritePass", "Overwrites a graph texture.");

    explicit OverwritePass(Falcor::ref<Falcor::Device> device, bool unorderedAccess) :
        Falcor::RenderPass(std::move(device)), unorderedAccess(unorderedAccess)
    {
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        reflection.addInputOutput("color", "Color target").bindFlags(
            unorderedAccess ? Falcor::ResourceBindFlags::UnorderedAccess : Falcor::ResourceBindFlags::None
        );
        return reflection;
    }

    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override
    {
        if (unorderedAccess)
        {
            static const Falcor::ChannelList kChannels{{"color", {}, "Color target"}};
            Falcor::clearRenderPassChannels(renderCommands, kChannels, renderData);
        }
        else
            renderCommands->clearTexture(renderData.getTexture("color").get(), Falcor::float4(0.f));
    }

private:
    bool unorderedAccess;
};

struct ReadbackPass : Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(ReadbackPass, "ReadbackPass", "Reads a graph output for validation.");

    explicit ReadbackPass(Falcor::ref<Falcor::Device> device) : Falcor::RenderPass(std::move(device)) {}

    VulkanResources::ImageHandle         color;
    VulkanResources::BufferHandle        buffer;
    uint32_t colorWidth = 0;
    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        static const Falcor::ChannelList kInputs{{"color", {}, "Color target"}};
        Falcor::addRenderPassInputs(reflection, kInputs, Falcor::ResourceBindFlags::None, {colorWidth, 0});
        reflection.addInputOutput("buffer", "Host-visible color buffer").rawBuffer(kExtent.width * kExtent.height * sizeof(Pixel));
        return reflection;
    }
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override
    {
        auto& cache    = *renderCommands->vulkanResources;
        auto& commands = *renderCommands->commands;

        buffer = cache.registerBuffer(renderData["buffer"]->asBuffer());
        cache.transition(commands, buffer, vulkan_access::kTransferWrite);
        color = cache.registerImage(renderData.getTexture("color"));
        cache.transition(commands, color, vulkan_access::kTransferSource);

        commands.copyImageToBuffer(
            cache.getImage(color).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            cache.getBuffer(buffer).handle(),
            vk::BufferImageCopy{
                .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
                .imageExtent      = kExtent,
            }
        );
    }
};

std::vector<Pixel> run(
    Falcor::PluginManager& plugins, VulkanContext& vulkan, const Falcor::RenderContext& deviceContext,
    const FalcorPostProcessConfig& settings, vk::Format format,
    PostProcessMode mode = PostProcessMode::Falcor, uint32_t sampleCount = 1
)
{
    FrameContext frame;
    frame.init(vulkan);
    FrameData       frameData;
    RendererConfig config;
    config.feature.postProcess = mode;
    const auto device = deviceContext.getDevice();
    auto graph = Falcor::RenderGraph::create(device);
    graph->addPass(Falcor::make_ref<SourcePass>(graph->getDevice(), sampleCount), "source");
    if (mode == PostProcessMode::Falcor)
    {
        auto tone = Falcor::RenderPass::create("FalcorPostProcess", device, Falcor::Properties::json(Serialize(settings)), plugins);
        graph->addPass(std::move(tone), "tone");
        graph->addEdge("source.color", "tone.source");
    }
    else
    {
        graph->addPass(Falcor::RenderPass::create("ExposurePass", device, {}, plugins), "exposure");
        graph->addPass(Falcor::RenderPass::create("ToneMappingPass", device, {}, plugins), "tone");
        graph->addEdge("source.color", "exposure.hdrColor");
        graph->addEdge("source.color", "tone.hdrColor");
        graph->addEdge("exposure.state", "tone.exposure");
    }
    const auto target = Falcor::make_ref<Falcor::Texture>(
        vulkan,
        vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = kExtent,
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
                vk::ImageUsageFlagBits::eTransferDst,
        },
        Falcor::ResourceBindFlags::RenderTarget
    );
    graph->markOutput("tone.color");
    // The readback must finish before the marked, in-place write to the same allocation.
    graph->addPass(Falcor::make_ref<OverwritePass>(graph->getDevice(), format == vk::Format::eR8G8B8A8Unorm), "overwrite");
    const auto readback = Falcor::make_ref<ReadbackPass>(graph->getDevice());
    graph->addPass(readback, "readback");
    const auto unused = Falcor::make_ref<SourcePass>(graph->getDevice());
    graph->addPass(unused, "unused");
    graph->addEdge("tone.color", "overwrite.color");
    graph->markOutput("overwrite.color");
    graph->addEdge("tone.color", "readback.color");
    graph->addEdge("readback", "overwrite");
    const auto createReadback = [&]
    {
        return device->createBuffer(
            kExtent.width * kExtent.height * sizeof(Pixel), Falcor::ResourceBindFlags::None, Falcor::MemoryType::ReadBack
        );
    };
    graph->markOutput("readback.buffer");
    Falcor::Fbo framebuffer;
    framebuffer.attachColorTarget(target, 0);
    graph->onResize(&framebuffer);
    VulkanResources cache{vulkan};
    auto compileContext = deviceContext;
    compileContext.vulkanResources = &cache;
    compileContext.config = &config;
    if (sampleCount > 1)
    {
        std::string log;
        CHECK(!graph->compile(&compileContext, log) && log.contains("missing graph input 'readback.buffer'"));
        CHECK(!graph->doesPassExist("source.color-ResolvePass"));

        graph->setInput("readback.buffer", target);
        CHECK(!graph->compile(&compileContext, log) && log.contains("resource kind mismatch at 'readback.buffer'"));
    }
    const auto initialReadback = createReadback();
    graph->setInput("readback.buffer", initialReadback);
    if (sampleCount > 1)
    {
        const auto invalid = Falcor::make_ref<ReadbackPass>(graph->getDevice());
        invalid->colorWidth = kExtent.width + 1;
        graph->addPass(invalid, "invalid");
        graph->addEdge("source.color", "invalid.color");
        graph->setInput("invalid.buffer", initialReadback);
        graph->markOutput("invalid.buffer");

        std::string log;
        CHECK(!graph->compile(&compileContext, log));
        CHECK(log.contains("cannot connect 'source.color-ResolvePass.dst' to 'invalid.color'"));
        CHECK(!graph->doesPassExist("source.color-ResolvePass"));
        graph->removePass("invalid");
    }
    CHECK(graph->compile(&compileContext));
    CHECK(!graph->doesPassExist("source.color-ResolvePass"));
    CHECK(unused->color.id == VulkanResources::kInvalidHandle);
    const auto color = graph->getOutput("tone.color")->asTexture();
    CHECK(color != target && color->imageInfo().format == format);
    CHECK(color->getName() == "tone.color");
    CHECK(color->getDevice() == device && target->getDevice() == device);
    CHECK(graph->getOutput("overwrite.color") == color);
    CHECK(initialReadback->getName().empty());
    CHECK(initialReadback->getDevice() == device);
    CHECK(initialReadback->getSize() == kExtent.width * kExtent.height * sizeof(Pixel));
    const RenderView       view;
    const PreparedScene       renderData;
    const EditorFrameInput editor;
    auto renderCommands = deviceContext;
    renderCommands.vulkanResources = &cache;
    renderCommands.config = &config;
    renderCommands.frameData = &frameData;
    renderCommands.view = &view;
    renderCommands.scene = &renderData;
    renderCommands.editor = &editor;
    renderCommands.gpuProfiler = &frame.gpuProfiler();
    renderCommands.deltaSeconds = 1.0f / 60.0f;
    Profiler profiler;
    for (uint32_t index = 0; index < 3; ++index)
    {
        frame.wait();
        if (index == 1)
        {
            const auto readbackBuffer = createReadback();
            graph->setInput("readback.buffer", readbackBuffer);
            CHECK(graph->getOutput("readback.buffer") == readbackBuffer);
        }
        cache.beginFrame(0);
        auto& commands = frame.commandBufferHandle();
        vkCheck(commands.reset());
        vkCheck(commands.begin({}));
        renderCommands.commands = &commands;
        constexpr uint32_t kExtraScopes = 70;
        const bool checkProfiling = index == 0 && sampleCount > 1;
        if (checkProfiling)
        {
            profiler.beginFrame();
            renderCommands.profiler = &profiler;
            frame.gpuProfiler().beginFrame(commands, &profiler);
            for (uint32_t scope = 0; scope < kExtraScopes; ++scope)
                GpuProfileScope timing{&frame.gpuProfiler(), commands, "Extra scope"};
        }
        cache.recordUploads(commands);
        graph->execute(&renderCommands);
        CHECK(&cache.getImage(readback->color) == graph->getOutput("overwrite.color")->asTexture().get());
        const auto readbackSlot = cache.registerBuffer(graph->getOutput("readback.buffer")->asBuffer());
        CHECK(readback->buffer == readbackSlot);
        cache.transition(commands, readbackSlot, vulkan_access::kHostRead);
        frame.gpuProfiler().endFrame(commands);
        vkCheck(commands.end());
        const auto command = *commands;
        vkCheck(vulkan.deviceHandle().resetFences(frame.drawFenceHandle()));
        vkCheck(vulkan.queueHandle().submit(
            vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command},
            frame.drawFenceHandle()
        ));
        if (checkProfiling)
        {
            frame.wait();
            frame.gpuProfiler().collect(&profiler);
            const auto bits =
                vulkan.physicalDeviceHandle().getQueueFamilyProperties()[vulkan.graphicsQueueFamilyIndex()].timestampValidBits;
            if (bits)
            {
                const auto& timings = profiler.gpuTimings();
                CHECK(timings.size() > kExtraScopes && timings.front().name == "source");
                CHECK(std::ranges::count(timings, "Extra scope", &ProfileTiming::name) == kExtraScopes);
            }
            profiler.endFrame(0.0);
            renderCommands.profiler = nullptr;
        }
    }
    frame.wait();
    std::vector<Pixel> pixels(kExtent.width * kExtent.height);
    graph->getOutput("readback.buffer")->asBuffer()->getBlob(pixels.data(), 0, pixels.size() * sizeof(Pixel));
    frame.reset();
    return pixels;
}

void near(Pixel actual, Pixel expected)
{
    for (size_t channel = 0; channel < actual.size(); ++channel)
        CHECK(
            std::abs(int(actual[channel]) - int(expected[channel])) <= 1,
            "pixel channel {}: expected {}, got {}",
            channel,
            expected[channel],
            actual[channel]
        );
}

void checkUnorderedAccessView(VulkanContext& vulkan, const Falcor::RenderContext& deviceContext)
{
    constexpr uint32_t kMipLevel = 1;
    constexpr uint32_t kSliceCount = 2;
    constexpr uint32_t kFirstValue = 17;
    constexpr uint32_t kSecondValue = 91;
    constexpr uint32_t kBufferWordCount = 4;
    constexpr float kBufferValue = 0.5f;
    auto& device = *deviceContext.getDevice();
    auto texture = device.createTexture2D(
        2, 2, Falcor::ResourceFormat::R32Uint, kSliceCount, 2, Falcor::ResourceBindFlags::UnorderedAccess
    );
    const auto view = texture->getUAV(kMipLevel, 1, 1);
    CHECK(view == texture->getUAV(kMipLevel, 1));
    CHECK(view->getResource() == texture.get());
    CHECK((view->getViewInfo() == Falcor::ResourceViewInfo{kMipLevel, 1, 1, 1}));
    CHECK(view->getNativeHandle().as<VkImageView>() != VK_NULL_HANDLE);

    auto storage = device.createBuffer(kBufferWordCount * sizeof(uint32_t), Falcor::ResourceBindFlags::UnorderedAccess);
    Falcor::Resource& resource = *storage;
    CHECK(resource.getDevice().get() == &device);
    CHECK(resource.getBindFlags() == Falcor::ResourceBindFlags::UnorderedAccess);
    const auto deviceRefs = device.refCount();
    resource.breakStrongReferenceToDevice();
    CHECK(device.refCount() == deviceRefs - 1 && resource.getDevice().get() == &device);
    const auto wholeBuffer = resource.getUAV();
    const auto bufferView = storage->getUAV(sizeof(uint32_t), 2 * sizeof(uint32_t));
    CHECK(wholeBuffer == storage->getUAV(0));
    CHECK((wholeBuffer->getViewInfo() == Falcor::ResourceViewInfo{0, Falcor::Buffer::kEntireBuffer}));
    CHECK((bufferView->getViewInfo() == Falcor::ResourceViewInfo{sizeof(uint32_t), 2 * sizeof(uint32_t)}));
    CHECK(bufferView->getResource() == &resource);
    CHECK(bufferView->getNativeHandle().as<VkBuffer>() == static_cast<VkBuffer>(storage->handle()));

    std::array<uint32_t, kSliceCount + kBufferWordCount> result{};
    const auto readback = device.createBuffer(sizeof(result), Falcor::ResourceBindFlags::None, Falcor::MemoryType::ReadBack);
    VulkanResources cache{vulkan};
    FrameContext frame;
    frame.init(vulkan);
    auto& commands = frame.commandBufferHandle();
    vkCheck(commands.begin({}));

    auto context = deviceContext;
    context.commands = &commands;
    context.vulkanResources = &cache;
    context.clearUAV(texture->getUAV(kMipLevel).get(), Falcor::uint4(kFirstValue));
    context.clearUAV(
        Falcor::UnorderedAccessView::create(&device, texture.get(), kMipLevel, 1, 1).get(), Falcor::uint4(kSecondValue)
    );
    context.clearUAV(wholeBuffer.get(), Falcor::uint4(kFirstValue));
    context.clearUAV(
        Falcor::UnorderedAccessView::create(&device, storage.get(), sizeof(uint32_t), 2 * sizeof(uint32_t)).get(),
        Falcor::float4(kBufferValue)
    );

    const auto buffer = cache.registerBuffer(readback);
    cache.transition(commands, buffer, vulkan_access::kTransferWrite);
    cache.transition(commands, cache.registerImage(texture), vulkan_access::kTransferSource);
    commands.copyImageToBuffer(
        texture->handle(), vk::ImageLayout::eTransferSrcOptimal, readback->handle(),
        vk::BufferImageCopy{
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, kMipLevel, 0, kSliceCount},
            .imageExtent = {1, 1, 1},
        }
    );
    cache.transition(commands, cache.registerBuffer(storage), VulkanResources::MemoryAccess{
        vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
    });
    commands.copyBuffer(storage->handle(), readback->handle(), vk::BufferCopy{
        .dstOffset = kSliceCount * sizeof(uint32_t), .size = storage->getSize(),
    });
    cache.transition(commands, buffer, vulkan_access::kHostRead);

    vkCheck(commands.end());
    const auto command = *commands;
    vkCheck(vulkan.deviceHandle().resetFences(frame.drawFenceHandle()));
    vkCheck(vulkan.queueHandle().submit(
        vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command}, frame.drawFenceHandle()
    ));
    frame.wait();
    readback->getBlob(result.data(), 0, sizeof(result));
    constexpr auto kBufferBits = std::bit_cast<uint32_t>(kBufferValue);
    CHECK((result == std::array{kFirstValue, kSecondValue, kFirstValue, kBufferBits, kBufferBits, kFirstValue}));

    {
        Falcor::Scripting::Context script;
        script.setObject("texture", Falcor::ref<Falcor::Resource>(texture));
        script.setObject("buffer", Falcor::ref<Falcor::Resource>(storage));
        Falcor::Scripting::runScript(
            "from falcor import Buffer, Device, MemoryType, ResourceBindFlags\n"
            "device = texture.device\n"
            "assert isinstance(device, Device) and buffer.device is device and texture.device is device\n"
            "created = device.create_buffer(16)\n"
            "assert isinstance(created, Buffer) and created.device is device\n"
            "assert created.memory_type is MemoryType.DeviceLocal and created.size == 16\n"
            "readback = device.create_buffer(size=8, bind_flags=ResourceBindFlags.None_, memory_type=MemoryType.ReadBack)\n"
            "assert readback.memory_type is MemoryType.ReadBack and readback.size == 8\n"
            "assert device.wait() is None",
            script
        );
        CHECK(script.getObject<Falcor::ref<Falcor::Device>>("device").get() == &device);
        CHECK(script.getObject<Falcor::ref<Falcor::Resource>>("created")->getBindFlags() == Falcor::ResourceBindFlags::None);
    }

    texture.reset();
    storage.reset();
    cache.releaseUnusedResources();
    CHECK(!view->getResource() && !view->getNativeHandle().isValid());
    CHECK(!wholeBuffer->getResource() && !bufferView->getNativeHandle().isValid());
    frame.reset();
}

void checkBufferMemoryTypes(Falcor::Device& device)
{
    using Falcor::MemoryType;
    using Falcor::ResourceBindFlags;
    constexpr std::array<uint32_t, 3> kInitial{21, 83, 172};
    auto gpu = device.createBuffer(sizeof(kInitial), ResourceBindFlags::None, MemoryType::DeviceLocal, kInitial.data());
    std::array<uint32_t, kInitial.size()> result{};
    gpu->getBlob(result.data(), 0, sizeof(result));
    CHECK(result == kInitial);

    constexpr uint32_t kReplacement = 119;
    gpu->setBlob(&kReplacement, sizeof(uint32_t), sizeof(kReplacement));
    gpu->getBlob(result.data(), 0, sizeof(result));
    CHECK((result == std::array{kInitial[0], kReplacement, kInitial[2]}));

    auto upload = device.createBuffer(sizeof(kInitial), ResourceBindFlags::None, MemoryType::Upload, kInitial.data());
    const auto* mapping = static_cast<const uint32_t*>(upload->map());
    CHECK(std::equal(kInitial.begin(), kInitial.end(), mapping));
    upload->setBlob(&kReplacement, sizeof(uint32_t), sizeof(kReplacement));
    CHECK(upload->map() == mapping && mapping[1] == kReplacement);
    upload->unmap();
}

void check(VulkanContext& vulkan, const Falcor::RenderContext& deviceContext)
{
    checkBufferMemoryTypes(*deviceContext.getDevice());
    checkUnorderedAccessView(vulkan, deviceContext);
    {
        VulkanResources cache{vulkan};
        auto& device = *deviceContext.getDevice();
        auto retired = device.createBuffer(sizeof(uint32_t), Falcor::ResourceBindFlags::UnorderedAccess);
        const auto retained = device.createBuffer(sizeof(uint32_t), Falcor::ResourceBindFlags::UnorderedAccess);
        const auto retiredSlot = cache.registerBuffer(retired);
        const auto retainedSlot = cache.registerBuffer(retained);
        CHECK(retired->refCount() == 2 && retained->refCount() == 2);
        retired.reset();
        cache.releaseUnusedResources();
        CHECK(&cache.getBuffer(retainedSlot) == retained.get());

        retained->setName("retained");
        cache.ensureBufferCapacity(retainedSlot, 2 * sizeof(uint32_t));
        CHECK(retained->getName() == "retained" && retained->getSize() == 2 * sizeof(uint32_t));
        CHECK(retained->getDevice().get() == &device && retained->getBindFlags() == Falcor::ResourceBindFlags::UnorderedAccess);

        const auto replacement = device.createBuffer(sizeof(uint32_t), Falcor::ResourceBindFlags::UnorderedAccess);
        CHECK(cache.registerBuffer(replacement) == retiredSlot);
    }

    Falcor::PluginManager plugins;
    plugins.loadAllPlugins();
    FalcorPostProcessConfig settings;
    // Independently evaluated from the original Falcor equations, including sRGB presentation.
    constexpr std::array<Pixel, 6> kExpected{
        Pixel{137, 255, 255, 128},
        {96, 183, 255, 128},
        {255, 255, 255, 128},
        {150, 216, 244, 128},
        {143, 213, 250, 128},
        {128, 214, 247, 128},
    };
    for (uint32_t mode = 0; mode < kExpected.size(); ++mode)
    {
        settings.toneMapper.mode = static_cast<FalcorToneMappingOperator>(mode);
        for (const auto format : {vk::Format::eR8G8B8A8Srgb, vk::Format::eR8G8B8A8Unorm})
            for (const auto pixel : run(plugins, vulkan, deviceContext, settings, format))
                near(pixel, kExpected[mode]);
        LOG_INFO("Falcor tone operator {} passed for sRGB and UNORM", mode);
    }
    settings.toneMapper              = {};
    settings.toneMapper.autoExposure = true;
    for (const auto pixel : run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Srgb))
        near(pixel, {6, 30, 93, 128});
    LOG_INFO("Falcor logarithmic luminance / auto exposure passed");

    auto& toneMapper                = settings.toneMapper;
    toneMapper                      = {};
    toneMapper.mode                 = FalcorToneMappingOperator::Linear;
    toneMapper.filmSpeed            = 200;
    toneMapper.fNumber              = 2;
    toneMapper.shutter              = 4;
    toneMapper.exposureCompensation = 1;
    for (const auto pixel : run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Unorm))
        near(pixel, {71, 137, 255, 128});
    toneMapper.exposureMode = FalcorExposureMode::AperturePriority;
    toneMapper.setExposureValue(5);
    CHECK(toneMapper.fNumber == 2 && toneMapper.shutter == 8);
    toneMapper.exposureMode = FalcorExposureMode::ShutterPriority;
    toneMapper.setExposureValue(3);
    CHECK(toneMapper.fNumber == 1 && toneMapper.shutter == 8);
    toneMapper              = {};
    toneMapper.whiteBalance = true;
    const auto identity     = toneMapper.whiteBalanceTransform();
    for (glm::length_t row = 0; row < 3; ++row)
        for (glm::length_t column = 0; column < 3; ++column)
            CHECK(std::abs(identity[column][row] - (row == column ? 1.0f : 0.0f)) < 0.00001f);
    toneMapper.whitePoint = 10000.0f;
    for (const auto pixel : run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Srgb))
        near(pixel, {161, 214, 244, 128});
    LOG_INFO("Falcor physical exposure and 6500 K white balance passed");

    toneMapper                        = {};
    auto& effects                     = settings.simplePostFx;
    effects.bloomAmount               = 0.2f;
    effects.starAmount                = 0.3f;
    effects.vignetteAmount            = 0.3f;
    effects.chromaticAberrationAmount = 0.5f;
    effects.barrelDistortAmount       = 0.25f;
    effects.saturationCurve           = {0.6f, 0.8f, 1.2f};
    effects.colorOffsetScalar         = 0.1f;
    effects.colorScaleScalar          = -0.1f;
    effects.colorPowerScalar          = 0.2f;
    effects.wipe                      = 0.5f;
    const auto pixels                 = run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Srgb);
    CHECK(pixels.front() != pixels.back());
    for (uint32_t y = 0; y < kExtent.height; ++y)
        for (uint32_t x = 0; x < kExtent.width; ++x)
            if (x < kExtent.width * effects.wipe)
                near(pixels[y * kExtent.width + x], kExpected[5]);
            else
                CHECK(pixels[y * kExtent.width + x][3] == 255);
    effects.enabled = false;
    for (const auto pixel : run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Srgb))
        near(pixel, kExpected[5]);
    LOG_INFO("Falcor 8-level bloom, stars, lens/color effects, wipe and bypass passed");

    const auto native = run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Srgb, PostProcessMode::RenderLab);
    const auto restored          = run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Srgb);
    CHECK(native.front() != restored.front());
    for (const auto pixel : restored)
        near(pixel, kExpected[5]);
    const auto resolved = run(plugins, vulkan, deviceContext, settings, vk::Format::eR8G8B8A8Srgb, PostProcessMode::Falcor, 4);
    CHECK(resolved == restored);
    LOG_INFO("Native / Falcor graph replacement passed");
}

void checkRendererSwitches(ConfigManager& config, SceneManager& scenes)
{
    const auto originalGraph = config.renderGraphName();
    const auto originalSettings = config.getSettings();
    selectGraphForPath(config, RenderPath::Forward);
    const auto forwardGraph = config.renderGraphName();
    auto parameters = config.getOptions().at("parameters");
    parameters.update({{"ibl", false}, {"shadows", false}, {"hdr", true},
                       {"postProcess", Serialize(PostProcessMode::RenderLab)}});
    ExposureConfig nativeExposure{.ev100 = 1.25f};
    FalcorPostProcessConfig settings;
    Renderer renderer;
    context().renderer = &renderer;
    renderer_test::setParameters(renderer, config, parameters);
    renderer_test::setPassProperties(renderer, config, "ExposurePass", "exposure", Serialize(nativeExposure));
    renderer.init();
    CHECK(renderer.defaultExposure() == nativeExposure);
    {
        const auto native = Falcor::RenderGraph::createFromFile(renderer.getActiveGraph()->getDevice(), config.renderGraphScript());
        CHECK(native && native->getName() == config.renderGraphName() && native->getOutputName(0) == "toneMapping.color");
        CHECK(native->getOutputCount() == 3);
        CHECK(native->getOutputName(1) == "exposure.state" && native->getOutputName(2) == "picking.pickingImage");
        CHECK(native->doesPassExist("toneMapping") && native->doesPassExist("picking"));
    }
    LOG_INFO("Native graph factory defaults and presentation output passed");
    const auto            extent = renderer.swapchainHandle().extent();
    EditorCameraConfig    cameraConfig;
    EditorCamera          camera{cameraConfig};
    const auto            view = camera.renderView({0, 0, float(extent.width), float(extent.height)});
    const SceneRenderData scene;
    for (uint32_t phase = 0; phase < 10; ++phase)
    {
        const auto mode = phase % 2 == 0 ? PostProcessMode::RenderLab : PostProcessMode::Falcor;
        parameters["postProcess"] = Serialize(mode);
        settings.toneMapper.autoExposure  = (phase & 2u) != 0;
        settings.simplePostFx.bloomAmount = (phase & 4u) != 0 ? 0.2f : 0.0f;
        settings.simplePostFx.enabled     = phase != 9;
        renderer_test::setParameters(renderer, config, parameters);
        nativeExposure.ev100 = 0.25f * static_cast<float>(phase);
        renderer_test::setPassProperties(renderer, config, "ExposurePass", "exposure", Serialize(nativeExposure));
        renderer_test::setPassProperties(renderer, config, "FalcorPostProcess", "falcor", Serialize(settings));
        for (uint32_t frame = 0; frame < 4; ++frame)
            CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
        CHECK(renderer.getActiveGraph()->doesPassExist("falcor") == (mode == PostProcessMode::Falcor));
        CHECK(renderer.defaultExposure() == (mode == PostProcessMode::RenderLab ? nativeExposure : ExposureConfig{}));
    }
    const auto forwardOptions = config.getOptions();
    selectGraphForPath(config, RenderPath::Deferred);
    const auto deferredParameters = config.getOptions().at("parameters");
    auto ldrParameters = deferredParameters;
    ldrParameters.update({{"hdr", false}, {"ibl", false}, {"shadows", false}});
    renderer_test::setParameters(renderer, config, ldrParameters);
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.getActiveGraph()->getName() == config.renderGraphName() && !config.rendererConfig().feature.hdr);
    renderer_test::setParameters(renderer, config, deferredParameters);

    config.setRenderGraph(forwardGraph);
    CHECK(config.getOptions() == forwardOptions);
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.getActiveGraph()->getName() == config.renderGraphName() && config.rendererConfig().feature.hdr);

    auto* falcor = renderer.getActiveGraph()->getPass("falcor").get();
    settings.toneMapper.autoExposure = !settings.toneMapper.autoExposure;
    const auto updatedSettings = Serialize(settings);
    renderer_test::setPassProperties(renderer, config, falcor->getType(), falcor->getName(), updatedSettings);
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.getActiveGraph()->getPass("falcor").get() == falcor);
    CHECK(nlohmann::json(falcor->getProperties().toJson()) == updatedSettings);

    const auto idleRevision = renderer.vulkanContext().getIdleRevision();
    settings.toneMapper.exposureCompensation += 0.5f;
    renderer_test::setPassProperties(renderer, config, falcor->getType(), falcor->getName(), Serialize(settings));
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.vulkanContext().getIdleRevision() == idleRevision);

    settings.simplePostFx.enabled = true;
    settings.simplePostFx.bloomAmount = 0.2f;
    falcor->setProperties(Falcor::Properties::json(Serialize(settings)));
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.vulkanContext().getIdleRevision() > idleRevision);
    CHECK(renderer.getActiveGraph()->getPass("falcor").get() == falcor);

    scenes.load(SceneAsset{});
    CHECK(renderer.getActiveGraph()->getScene() != scenes.getScene());
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.getActiveGraph()->getScene() == scenes.getScene());
    CHECK(renderer.getActiveGraph()->getPass("falcor").get() == falcor);

    renderer.recreateSwapchain();
    CHECK(renderer.getActiveGraph()->getPass("falcor").get() == falcor);
    CHECK(renderer.supportsObjectPicking());
    CHECK(renderer.getActiveGraph()->getOutput("picking.pickingImage")->asTexture()->getFormat() == Falcor::ResourceFormat::R32Uint);
    {
        Falcor::Scripting::Context script;
        script.setObject("graph", Falcor::ref<Falcor::RenderGraph>(renderer.getActiveGraph()));
        Falcor::Scripting::runScript(
            "from falcor import ResourceFormat\n"
            "assert graph.get_output('picking.pickingImage').format is ResourceFormat.R32Uint",
            script
        );
    }
    uint32_t overlayCalls = 0;
    const EditorFrameInput pick{
        .requestPick = true,
        .pickX = extent.width / 2,
        .pickY = extent.height / 2,
        .recordUi = [&](VkCommandBuffer) { ++overlayCalls; },
    };
    const auto picked = renderer.render(view, scene, pick, 1.0f / 60.0f);
    CHECK(!picked.swapchainNeedsRebuild && picked.hasPickResult && picked.pickedSelectionId == 0);
    CHECK(overlayCalls == 1);
    renderer_test::setPassProperties(renderer, config, falcor->getType(), falcor->getName(), nlohmann::json::object());
    CHECK(!renderer.render(view, scene, {}, 1.0f / 60.0f).swapchainNeedsRebuild);
    CHECK(renderer.getActiveGraph()->getPass("falcor").get() == falcor);
    CHECK(nlohmann::json(falcor->getProperties().toJson()) == Serialize(FalcorPostProcessConfig{}));
    LOG_INFO("Graph selection, isolated options and property reset passed");

    renderer.shutdown();
    context().renderer = nullptr;
    config.getSettings() = originalSettings;
    config.setRenderGraph(originalGraph);
    LOG_INFO("Renderer runtime post process switching passed (10 phases, 40 frames)");
}
}  // namespace

int main(int, char** argv)
{
    logger::init(argv[0]);
    ConfigManager config;
    context().config = &config;
    config.init();
    const auto originalEditor = config.editorConfig();
    config.setEditorSize(64, 64);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    Window window;
    window.init();
    VulkanContext vulkan;
    vulkan.init(window);
    SceneManager scenes;
    scenes.init();
    AssetManager assets;
    assets.init(config.paths().assets);
    AssetDataManager data;
    data.init(config.paths().assets);
    ShaderManager shaders;
    shaders.init(vulkan.deviceHandle());
    const auto device = Falcor::make_ref<Falcor::Device>();
    device->init(vulkan, shaders);
    RenderResourceManager resources;
    resources.init(vulkan, assets, data);
    device->getRenderContext()->resources = &resources;
    check(vulkan, *device->getRenderContext());
    resources.shutdown();
    device->shutdown();
    shaders.shutdown();
    vulkan.reset();
    context().window           = &window;
    context().assetManager     = &assets;
    context().assetDataManager = &data;
    context().sceneManager     = &scenes;
    checkRendererSwitches(config, scenes);
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.setEditorSize(originalEditor.width, originalEditor.height);
    config.shutdown();
    context() = {};
}
