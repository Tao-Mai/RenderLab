// Manual GPU check: visibility along the finite segment to a point light.
#include "asset/AssetDataManager.h"
#include "asset/AssetManager.h"
#include "asset/BuiltinAssets.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Window.h"
#include "render/RenderContext.h"
#include "render/Renderer.h"
#include "render/device/VkCheck.h"
#include "render/feature/EndPass.h"
#include "render/raytracing/RayTracingPass.h"
#include "scene/SceneManager.h"
#include "scene/SceneTypes.h"
#include "scene/system/RenderSystem.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/packing.hpp>

namespace
{
struct PixelReadbackPass
{
    RenderGraph::ImageHandle  image;
    RenderGraph::BufferHandle buffer;

    RenderGraph::BufferHandle setup(RenderGraph& graph, RenderGraph::ImageHandle source)
    {
        image  = source;
        buffer = graph.createBuffer({
            .size             = sizeof(uint16_t) * 4,
            .memoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        });
        buffer = graph.write(buffer);
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
            {.stages = vk::PipelineStageFlagBits2::eTransfer, .access = vk::AccessFlagBits2::eTransferWrite}
        );
        return buffer;
    }

    void execute(RenderGraph& graph) const
    {
        const auto                extent = graph.imageInfo(image).extent;
        const vk::BufferImageCopy region{
            .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .layerCount = 1},
            .imageOffset      = {static_cast<int32_t>(extent.width / 2), static_cast<int32_t>(extent.height / 2), 0},
            .imageExtent      = {1, 1, 1},
        };
        graph.commands().copyImageToBuffer(
            graph.getImage(image).handle(),
            vk::ImageLayout::eTransferSrcOptimal,
            graph.getBuffer(buffer).handle(),
            region
        );
    }
};

void checkVisibility(Renderer& renderer, SceneManager& scenes)
{
    auto& vulkan = renderer.vulkanContext();
    CHECK(vulkan.supportsRayTracing());
    std::array<FrameContext, maxFramesInFlight> frames;
    for (auto& frame : frames)
        frame.init(vulkan, *renderContext().descriptors);

    RenderGraph       graph;
    RayTracingPass    trace;
    PixelReadbackPass readback;
    EndPass           end;
    RenderSystem      renderSystem;
    graph.init(vulkan, renderer.swapchainHandle(), frames);
    const auto output = graph.addPass(trace);
    CHECK(graph.imageInfo(output.rayImage).format == kHdrColorFormat);
    const auto buffer = graph.addPass(readback, output.rayImage);
    graph.addPass(end, RenderGraph::ImageHandle{}, buffer);
    graph.compile();

    const auto extent = renderer.swapchainHandle().extent();
    const auto view =
        scenes.editorCamera().renderView({0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height)});
    const auto sample = [&]
    {
        const auto& data =
            renderSystem.prepare(scenes.scene(), scenes.editorCamera().worldPosition(), *renderContext().resources);
        frames[0].data.updateView(view.uniforms);
        graph.execute(view, data, {}, 0, 0);
        const auto           commandBuffer = *frames[0].commandBufferHandle();
        const vk::SubmitInfo submitInfo{.commandBufferCount = 1, .pCommandBuffers = &commandBuffer};
        vkCheck(vulkan.queueHandle().submit(submitInfo));
        vkCheck(vulkan.queueHandle().waitIdle());
        std::array<uint16_t, 4> pixel;
        graph.getBuffer(buffer, 0).download(pixel.data(), sizeof(pixel));
        return glm::unpackHalf1x16(pixel[0]);
    };

    const auto clear = sample();
    CHECK(clear > 1.0f, "HDR surface radiance is unexpectedly clipped or dark: {}", clear);

    auto& blocker                = scenes.createActor("AStaticMesh");
    blocker.transform().position = {1, 0, 2};
    blocker.transform().scale    = glm::vec3{0.6f};
    const auto blocked           = sample();
    CHECK(blocked == 0, "blocker did not shadow the surface: {}", blocked);

    blocker.transform().position = {4, 0, 5};
    const auto beyondLight       = sample();
    CHECK(beyondLight == clear, "geometry behind the light changed visibility: {} vs {}", beyondLight, clear);
    LOG_INFO("Ray tracing visibility passed: clear {}, blocked {}, beyond light {}", clear, blocked, beyondLight);

    graph.reset();
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
    data.init();
    scenes.init();
    assets.init();
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window.init();

    SceneAsset scene;
    scene.editorCamera.position                     = {0, 0, 5};
    scene.editorCamera.yaw                          = -90;
    scene.editorCamera.pitch                        = 0;
    auto sphere                                     = std::make_unique<AStaticMesh>();
    sphere->getComponent<RenderComponent>()->meshId = BuiltinAssets::Mesh::sphere;
    scene.actors.push_back(std::move(sphere));
    auto light                                       = std::make_unique<ALight>();
    light->transform().position                      = {2, 0, 3};
    light->getComponent<LightComponent>()->intensity = 100;
    scene.actors.push_back(std::move(light));
    scenes.load(scene);
    renderer.init();
    checkVisibility(renderer, scenes);
    renderer.shutdown();
    scenes.shutdown();
    assets.shutdown();
    data.shutdown();
    window.shutdown();
    config.shutdown();
    context() = {};
}
