#include "MinimalPathTracerSettings.h"

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "editor/PassEditor.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassHelpers.h"
#include "render/graph/RenderPassStandardFlags.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/raytracing/RealtimeScene.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <optional>
#include <vector>

namespace
{
constexpr std::array      kRaygenEntries{"raygenMain"};
const Falcor::ChannelList kOutputChannels{
    {"color", "outputImage", "Path traced radiance", false, rhi::Format::RGBA16Float},
};

class MinimalPathTracer : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(MinimalPathTracer, "MinimalPathTracer", "Minimal path tracer.");

    MinimalPathTracer(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties) :
        Falcor::RenderPass(std::move(device)),
        parameters(Falcor::deserializeFromProperties<MinimalPathTracerSettings>(properties))
    {
        parameters.validate();
    }

    [[nodiscard]] Falcor::Properties getProperties() const override
    {
        return Falcor::serializeToProperties(parameters);
    }

    void setProperties(const Falcor::Properties& properties) override
    {
        MinimalPathTracerSettings next;
        Deserialize(nlohmann::json(properties.toJson()), next);
        next.validate();
        parameters = std::move(next);
    }

    static Falcor::ref<MinimalPathTracer> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
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
        auto&      shaderManager = *passContext->shaderManager;
        const auto shader        = shaderManager.getOrLoad("minimal_path_tracer");
        shaderManager.layout(shader).validateConstants<PathTracingParameters>("parameters");
        scene.compile(*passContext);
        if (!pipeline)
        {

            const std::array groups{
                rhi::HitGroupDesc{"hitGroup0", "closestHitMain"},
                rhi::HitGroupDesc{"hitGroup1", "lightClosestHitMain"}
            };
            constexpr std::array        kGroupNames{"hitGroup0", "hitGroup1"};
            rhi::RayTracingPipelineDesc desc;
            desc.program           = shaderManager.program(shader);
            desc.hitGroups         = groups.data();
            desc.hitGroupCount     = uint32_t(groups.size());
            desc.maxRecursion      = kRayPipelineRecursionDepth;
            desc.maxRayPayloadSize = kPathTracingPayloadBytes;
            rhiCheck(mpDevice->createRayTracingPipeline(desc, pipeline.writeRef()));
            rhi::ShaderTableDesc tableDesc;
            tableDesc.program                     = desc.program;
            tableDesc.rayGenShaderEntryPointNames = kRaygenEntries.data();
            tableDesc.rayGenShaderCount           = uint32_t(kRaygenEntries.size());
            tableDesc.missShaderEntryPointNames   = kPathTracingMissEntries.data();
            tableDesc.missShaderCount             = uint32_t(kPathTracingMissEntries.size());
            tableDesc.hitGroupNames               = kGroupNames.data();
            tableDesc.hitGroupCount               = uint32_t(kGroupNames.size());
            rhiCheck(mpDevice->createShaderTable(tableDesc, shaderTable.writeRef()));
        }
        sampleIndex        = 0;
        previousMaxBounces = parameters.maxBounces;
    }

    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override
    {
        if (previousMaxBounces != parameters.maxBounces)
        {
            auto&      dictionary = renderData.getDictionary();
            const auto flags =
                dictionary.getValue(Falcor::kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
            dictionary[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
            previousMaxBounces                          = parameters.maxBounces;
        }

        auto&       cache    = *renderCommands->gpuResources;
        auto&       commands = *renderCommands->commands;
        const auto& frame    = *renderCommands;

        scene.prepare(cache, frame);
        cache.recordUploads(commands);
        scene.execute(commands, frame);

        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        auto* object = pass->bindPipeline(pipeline, shaderTable);
        CHECK(object);
        scene.bind(object, cache, frame);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["outputImage"].setBinding(renderData.getTexture("color")->getDefaultView()));
        const auto constants = makePathTracingParameters(frame, parameters.maxBounces, sampleIndex++);
        rhiCheck(root["parameters"].getDereferenced().setData(constants));
        const auto extent = renderData.getDefaultTextureDims();
        pass->dispatchRays(0, extent.x, extent.y, 1);
        pass->end();
    }

private:
    MinimalPathTracerSettings parameters;

    RealtimeScene                         scene;
    rhi::ComPtr<rhi::IRayTracingPipeline> pipeline;
    rhi::ComPtr<rhi::IShaderTable>        shaderTable;
    uint32_t                              sampleIndex        = 0;
    uint32_t                              previousMaxBounces = 0;
};

bool drawSettings(
    Falcor::Gui::Widgets&      widgets,
    MinimalPathTracerSettings& settings,
    const MinimalPathTracer&,
    const PassEditorContext&
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
