#include "MinimalPathTracerSettings.h"
#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassHelpers.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/raytracing/RayTracingPipeline.h"
#include "render/raytracing/RayTracingStorageImages.h"
#include "render/raytracing/RealtimeSceneBindings.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <optional>
#include <vector>

namespace
{
constexpr std::array kRaygenEntries{"raygenMain"};
const Falcor::ChannelList kOutputChannels{
    {"color", "outputImage", "Path traced radiance", false, Falcor::ResourceFormat::RGBA16Float},
};

class MinimalPathTracer : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(MinimalPathTracer, "MinimalPathTracer", "Minimal path tracer.");

    MinimalPathTracer(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device)), parameters(Falcor::deserializeFromProperties<MinimalPathTracerSettings>(properties))
    {
        parameters.validate();
    }

    [[nodiscard]] Falcor::Properties getProperties() const override { return Falcor::serializeToProperties(parameters); }

    void setProperties(const Falcor::Properties& properties) override
    {
        MinimalPathTracerSettings next;
        Deserialize(nlohmann::json(properties.toJson()), next);
        next.validate();
        parameters = std::move(next);
    }

    static Falcor::ref<MinimalPathTracer> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<MinimalPathTracer>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData&) override
    {
        Falcor::RenderPassReflection reflection;
        Falcor::addRenderPassOutputs(reflection, kOutputChannels);
        return reflection;
    }

    void compile(Falcor::RenderContext* passContext, const CompileData&) override
    {
        auto& cache = *passContext->vulkanResources;

        auto&       shaderManager = *passContext->shaderManager;
        const auto  shader        = shaderManager.getOrLoad("minimal_path_tracer");
        const auto& layout        = shaderManager.layout(shader);
        frameConstants = layout.pushConstants<PathTracingParameters>("parameters", vk::ShaderStageFlagBits::eRaygenKHR);

        const auto geometry = scene.compile(*passContext);
        traceAccesses.clear();
        sceneBindings.compile(traceAccesses, layout, geometry);
        traceAccesses.declare(cache);
        colorBinding = getRayTracingStorageImageBinding(
            *passContext->vulkan,
            layout,
            kOutputChannels.front().texname,
            kHdrColorFormat,
            kPathTracingColorFeatures
        );

        auto& device = *mpDevice;
        if (!pipeline)
        {
            std::vector<vk::DescriptorSetLayoutBinding> bindings;
            sceneBindings.appendLayoutBindings(bindings);
            bindings.push_back(colorBinding.layout(vk::ShaderStageFlagBits::eRaygenKHR));
            descriptorLayout          = device.createDescriptorSetLayout(bindings);
            const auto pipelineLayout = RealtimeSceneBindings::getPipelineLayout(
                device,
                layout,
                descriptorLayout,
                std::array{frameConstants.range}
            );
            pipeline.emplace(
                *passContext->vulkan,
                RayTracingPipeline::Description{
                    .shader            = shaderManager.module(shader),
                    .layout            = pipelineLayout,
                    .raygen            = kRaygenEntries,
                    .miss              = kPathTracingMissEntries,
                    .closestHit        = kPathTracingHitEntries,
                    .maxRecursionDepth = kRayPipelineRecursionDepth,
                }
            );
        }
        descriptorSets.clear();
        descriptorSets.reserve(maxFramesInFlight);
        for (uint32_t index = 0; index < maxFramesInFlight; ++index)
            descriptorSets.push_back(device.allocateDescriptorSet(descriptorLayout));

        resourcesBound     = false;
        sampleIndex        = 0;
        previousMaxBounces = parameters.maxBounces;
    }

    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override
    {
        if (previousMaxBounces != parameters.maxBounces)
        {
            auto& dictionary = renderData.getDictionary();
            const auto flags = dictionary.getValue(Falcor::kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
            dictionary[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
            previousMaxBounces = parameters.maxBounces;
        }

        auto&       cache    = *renderCommands->vulkanResources;
        auto&       commands = *renderCommands->commands;
        const auto& frame    = *renderCommands;
        if (!resourcesBound)
        {
            const auto     color     = cache.registerImage(renderData.getTexture(kOutputChannels.front().name));
            constexpr auto kRead     = vulkan_access::storageRead(kRayTracingStages);
            barriers.clear();
            barriers.add(frame.frameData->lightBufferHandle(), kRead);
            barriers.add(frame.materialTable->bufferHandle(), kRead);
            barriers.add(color, kRayTracingImageWrite);
            barriers.declare(cache);

            for (const auto& set : descriptorSets)
                cache.writeDescriptor(*set, colorBinding.index, color, vk::DescriptorType::eStorageImage);
            resourcesBound = true;
        }

        barriers.transition(cache, commands);

        scene.prepare(cache, frame);
        sceneBindings.prepare(cache, frame, *descriptorSets[frame.frameIndex]);
        cache.recordUploads(commands);
        scene.execute(cache, commands, frame);
        traceAccesses.transition(cache, commands);
        pipeline->bind(commands);
        sceneBindings.bind(commands, frame, pipeline->layout(), *descriptorSets[frame.frameIndex]);

        const auto constants = makePathTracingParameters(frame, parameters.maxBounces, sampleIndex++);
        frameConstants.push(commands, pipeline->layout(), constants);
        const auto extent = renderData.getDefaultTextureDims();
        pipeline->dispatch(commands, 0, {extent.x, extent.y, 1});
    }

private:
    MinimalPathTracerSettings parameters;

    ResourceBarriers                                 barriers;
    RealtimeScene                                    scene;
    RealtimeSceneBindings                            sceneBindings;
    ResourceBarriers                                 traceAccesses;
    DescriptorSetLayout                              descriptorLayout;
    std::optional<RayTracingPipeline>                pipeline;
    ShaderPushConstants<PathTracingParameters>       frameConstants;
    std::vector<vk::raii::DescriptorSet>             descriptorSets;
    ShaderBinding<vk::DescriptorType::eStorageImage> colorBinding;
    bool                                             resourcesBound = false;
    uint32_t                                         sampleIndex    = 0;
    uint32_t                                         previousMaxBounces = 0;
};

bool drawSettings(
    Falcor::Gui::Widgets& widgets, MinimalPathTracerSettings& settings, const MinimalPathTracer&, const PassEditorContext&
)
{
    const bool edited = widgets.slider("Max bounces", settings.maxBounces, kMinPathBounces, kMaxPathBounces);
    widgets.tooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
    return edited;
}
}  // namespace

void MinimalPathTracer::renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context->passEditor, "MinimalPathTracer");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<Falcor::RenderPass, MinimalPathTracer>();
}
