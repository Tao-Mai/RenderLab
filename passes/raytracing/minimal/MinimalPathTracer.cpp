#include "MinimalPathTracerSettings.h"

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "editor/PassEditor.h"
#include "render/GpuScene.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/raytracing/PathTracingShader.h"
#include "render/resources/MaterialTable.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/ShaderManager.h"

#include <array>
#include <optional>
#include <vector>

namespace
{
constexpr std::array kRaygenEntries{"raygenMain"};
constexpr char       kOutputColor[] = "color";

class MinimalPathTracer : public RenderPass
{
    RL_PLUGIN_CLASS(MinimalPathTracer, "MinimalPathTracer", "Minimal path tracer.");

    MinimalPathTracer(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
        RenderPass(std::move(device)), parameters(deserializeFromProperties<MinimalPathTracerSettings>(properties))
    {
        parameters.validate();
    }

    [[nodiscard]] Properties getProperties() const override
    {
        return serializeToProperties(parameters);
    }

    void setProperties(const Properties& properties) override
    {
        MinimalPathTracerSettings next;
        Deserialize(nlohmann::json(properties.toJson()), next);
        next.validate();
        parameters = std::move(next);
    }

    static ref<MinimalPathTracer> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<MinimalPathTracer>(std::move(device), properties);
    }

    void renderUI(const PassEditorContext& context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData&) override
    {
        RenderPassReflection reflection;
        reflection.addOutput(kOutputColor, rhi::TextureDesc{
            .size = {0, 0, 1},
            .format = rhi::Format::RGBA16Float,
            .usage = rhi::TextureUsage::UnorderedAccess,
        });
        return reflection;
    }

    void compile(RenderContext*, const CompileData&) override
    {
        if (!checkRayTracingSupport(*mpDevice))
            return;

        sampleIndex = 0;
    }

    void execute(RenderContext* renderCommands, const RenderData& renderData) override
    {
        auto&       commands = *renderCommands->commands;
        const auto& frame    = *renderCommands;
        const auto rayTracing = frame.scene->prepareRayTracing(commands, frame.frameIndex);
        const auto sceneGeneration = frame.scene->sceneGeneration();
        if (sceneGeneration != previousSceneGeneration)
        {
            sampleIndex             = 0;
            previousSceneGeneration = sceneGeneration;
        }

        const auto environment = pathEnvironment(*frame.scene);
        auto&      pipeline    = pipelines[static_cast<size_t>(environment)];
        if (!pipeline)
        {
            auto& shaderManager = *frame.shaderManager;
            auto* program = shaderManager.getOrCompile("minimal_path_tracer", pathEnvironmentDefines(environment));
            ShaderLayout(*program).validateConstants<PathTracingParameters>("parameters");
            rhiCheck(mpDevice->createRayTracingPipeline(pathTracingPipelineDesc(program), pipeline.writeRef()));
            if (!shaderTable)
                shaderTable = createPathTracingShaderTable(*mpDevice, program, kRaygenEntries);
        }

        auto* pass = commands.beginRayTracingPass();
        CHECK(pass);
        auto* object = pass->bindPipeline(pipeline, shaderTable);
        CHECK(object);
        rayTracing.bind(object);
        const rhi::ShaderCursor root(object);
        rhiCheck(root["outputImage"].setBinding(renderData.getTexture(kOutputColor)));
        const auto constants = makePathTracingParameters(frame, parameters.maxBounces, sampleIndex++);
        rhiCheck(root["parameters"].getDereferenced().setData(constants));
        const auto extent = renderData.getDefaultTextureDims();
        pass->dispatchRays(0, extent.x, extent.y, 1);
        pass->end();
    }

private:
    MinimalPathTracerSettings parameters;

    std::array<rhi::ComPtr<rhi::IRayTracingPipeline>, static_cast<size_t>(PathEnvironment::Count)> pipelines;
    rhi::ComPtr<rhi::IShaderTable> shaderTable;

    uint32_t sampleIndex             = 0;
    uint64_t previousSceneGeneration = 0;
};

bool drawSettings(
    Gui::Widgets&      widgets,
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

void MinimalPathTracer::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    drawPassEditor<drawSettings>(widget, *this, parameters, context, "MinimalPathTracer");
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<RenderPass, MinimalPathTracer>();
}
