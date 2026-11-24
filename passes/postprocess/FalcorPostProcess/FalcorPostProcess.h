#pragma once

#include "render/graph/RenderPassSettings.h"

#include "passes/postprocess/FalcorPostProcess/FalcorPostProcessConfig.h"

#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>

#include <vector>

// Connects Falcor's SimplePostFX and ToneMapper, followed by swapchain presentation.
class FalcorPostProcess : public RenderPass
{
public:
    FalcorPostProcess(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<FalcorPostProcess> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<FalcorPostProcess>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    FalcorPostProcessConfig parameters;

    void onSettingsUpdated(const FalcorPostProcessConfig& previous);

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

    static bool drawSettings(
        Gui::Widgets& widgets,
        FalcorPostProcessConfig& settings,
        const FalcorPostProcess& pass,
        const PassEditorContext& context
    );

    static const RenderPassReflection::Field kPorts[2];

    RL_PLUGIN_CLASS(
        FalcorPostProcess,
        "FalcorPostProcess",
        .desc = "Applies Falcor tone mapping and post effects.",
        .ports = kPorts,
        .settings = pass_settings::bind<
            &FalcorPostProcess::parameters,
            &FalcorPostProcess::drawSettings,
            &FalcorPostProcess::onSettingsUpdated
        >()
    );
};
