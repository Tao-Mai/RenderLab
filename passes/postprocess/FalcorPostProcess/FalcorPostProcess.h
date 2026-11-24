#pragma once

#include "render/graph/RenderPass.h"

#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <vector>

// Connects Falcor's SimplePostFX and ToneMapper, followed by swapchain presentation.
class FalcorPostProcess : public RenderPass
{
    RL_PLUGIN_CLASS(FalcorPostProcess, "FalcorPostProcess", "Applies Falcor tone mapping and post effects.");

    FalcorPostProcess(rhi::ComPtr<rhi::IDevice> device, const Properties& properties);

    [[nodiscard]] Properties getProperties() const override;
    void                             setProperties(const Properties& properties) override;

    static ref<FalcorPostProcess> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<FalcorPostProcess>(std::move(device), properties);
    }

    void renderUI(const PassEditorContext& context, Gui::Widgets& widget) override;

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    FalcorPostProcessConfig parameters;

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

        ImageStage(const FalcorPostProcess& owner, Stage stage, rhi::IShaderProgram* shader) :
            owner(owner), stage(stage), shader(shader)
        {
        }

        void compile(rhi::ITexture* source, rhi::ITexture* filtered, rhi::ITexture* target);
        void execute(rhi::ICommandEncoder& commands, rhi::ITexture* colorOutput) const;

    private:
        void dispatch(rhi::ICommandEncoder& commands) const;

        const FalcorPostProcess&  owner;
        Stage                     stage;
        rhi::IComputePipeline*    compute = nullptr;
        rhi::IShaderProgram*      shader = nullptr;
        uint32_t                  level = 0;
        rhi::ComPtr<rhi::ITexture>        target;
        rhi::ComPtr<rhi::IShaderObject>   shaderObject;
        rhi::ComPtr<rhi::IRenderPipeline> pipeline;
    };

    void initialize(const RenderContext& context);
    rhi::ITexture* addSimplePostFx(const RenderData& data, rhi::ITexture* source);
    rhi::ITexture* addToneMapper(ShaderManager& shaders, const RenderData& data, rhi::ITexture* source);
    rhi::ITexture*
    addImageStage(ImageStage pass, rhi::ITexture* source, rhi::ITexture* filtered, rhi::ITexture* target);

    rhi::ComPtr<rhi::IComputePipeline> downsample;
    rhi::ComPtr<rhi::IComputePipeline> upsample;
    rhi::ComPtr<rhi::IComputePipeline> postFx;
    rhi::ComPtr<rhi::ISampler>         borderSampler = nullptr;
    rhi::ComPtr<rhi::ISampler>         linearSampler = nullptr;
    rhi::ComPtr<rhi::ISampler>         pointSampler  = nullptr;
    bool                               encodeSrgb    = false;
    std::vector<ImageStage>            imageStages;
};
