#pragma once

#include "render/device/GpuResources.h"
#include "render/graph/RenderPass.h"

#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <vector>

// Connects Falcor's SimplePostFX and ToneMapper, followed by swapchain presentation.
class FalcorPostProcess : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(FalcorPostProcess, "FalcorPostProcess", "Applies Falcor tone mapping and post effects.");

    FalcorPostProcess(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void                             setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<FalcorPostProcess> create(rhi::ComPtr<rhi::IDevice> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<FalcorPostProcess>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    FalcorPostProcessConfig parameters;
    ShaderManager*          shaders = nullptr;

    enum class Stage
    {
        Downsample,
        Upsample,
        PostFx,
        Luminance,
        ToneMapping,
        Present,
    };

    class ImageStage
    {
    public:
        ImageStage(const FalcorPostProcess& owner, Stage stage, rhi::IComputePipeline* compute, uint32_t level = 0) :
            owner(owner), stage(stage), compute(compute), level(level)
        {
        }

        ImageStage(const FalcorPostProcess& owner, Stage stage, ShaderHandle shader) :
            owner(owner), stage(stage), shader(shader)
        {
        }

        void compile(
            GpuResources&             cache,
            GpuResources::ImageHandle source,
            GpuResources::ImageHandle filtered,
            GpuResources::ImageHandle target
        );
        void execute(GpuResources& cache, rhi::ICommandEncoder& commands, GpuResources::ImageHandle colorOutput);

    private:
        void dispatch(GpuResources& cache, rhi::ICommandEncoder& commands) const;

        const FalcorPostProcess&  owner;
        Stage                     stage;
        rhi::IComputePipeline*    compute = nullptr;
        ShaderHandle              shader;
        uint32_t                  level = 0;
        GpuResources::ImageHandle storageTarget;

        GpuResources::ImageHandle target;

        rhi::ComPtr<rhi::IShaderObject>   shaderObject;
        rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    };

    void initialize(const Falcor::RenderContext& context);
    GpuResources::ImageHandle
    addSimplePostFx(GpuResources& cache, const Falcor::RenderData& data, GpuResources::ImageHandle source);
    GpuResources::ImageHandle addToneMapper(
        ShaderManager& shaders, GpuResources& cache, const Falcor::RenderData& data, GpuResources::ImageHandle source
    );
    GpuResources::ImageHandle addImageStage(
        GpuResources&             cache,
        ImageStage                pass,
        GpuResources::ImageHandle source,
        GpuResources::ImageHandle filtered,
        GpuResources::ImageHandle target
    );

    rhi::ComPtr<rhi::IComputePipeline> downsample;
    rhi::ComPtr<rhi::IComputePipeline> upsample;
    rhi::ComPtr<rhi::IComputePipeline> postFx;
    rhi::ComPtr<rhi::ISampler>         borderSampler = nullptr;
    rhi::ComPtr<rhi::ISampler>         linearSampler = nullptr;
    rhi::ComPtr<rhi::ISampler>         pointSampler  = nullptr;
    bool                               encodeSrgb    = false;
    std::vector<ImageStage>            imageStages;
};
