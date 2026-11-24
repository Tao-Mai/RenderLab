#pragma once
#include <slang-rhi.h>

#include "passes/denoise/SVGF/SVGFFilter.h"
#include "render/graph/RenderPassSettings.h"

#include <array>

class SVGFPass : public RenderPass
{
public:
    SVGFPass(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties);

    static std::shared_ptr<SVGFPass> create(rhi::ComPtr<rhi::IDevice> device, const nlohmann::json& properties)
    {
        return std::make_shared<SVGFPass>(std::move(device), properties);
    }

    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    SvgfSettings parameters;

    SVGFReprojection                                            reprojection{parameters};
    SVGFFilter                                                  filter;
    std::array<rhi::ComPtr<rhi::ITexture>, kLightingLayerCount> reprojected;
    std::array<rhi::ComPtr<rhi::ITexture>, kLightingLayerCount> scratch;
    rhi::ComPtr<rhi::ITexture>                                  historyLength;
    bool                                                        resourcesBound = false;

    static const RenderPassReflection::Field kPorts[9];

    RL_PLUGIN_CLASS(
        SVGFPass,
        "SVGFPass",
        .desc = "SVGF denoising pass.",
        .ports = kPorts,
        .settings = pass_settings::bind<&SVGFPass::parameters>()
    );
};
