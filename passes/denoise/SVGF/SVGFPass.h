#pragma once

#include "passes/denoise/SVGF/SVGFFilter.h"
#include "render/graph/RenderPass.h"

#include <array>

class SVGFPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(SVGFPass, "SVGFPass", "SVGF denoising pass.");

    SVGFPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties);

    [[nodiscard]] Falcor::Properties getProperties() const override;
    void setProperties(const Falcor::Properties& properties) override;

    static Falcor::ref<SVGFPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<SVGFPass>(std::move(device), properties);
    }

    void renderUI(Falcor::RenderContext* context, Falcor::Gui::Widgets& widget) override;

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    SvgfSettings parameters;

    SVGFReprojection reprojection{parameters};
    SVGFFilter       filter;
    std::array<Falcor::ref<Falcor::Texture>, kLightingLayerCount> reprojected;
    std::array<Falcor::ref<Falcor::Texture>, kLightingLayerCount> scratch;
    Falcor::ref<Falcor::Texture> historyLength;
    bool             resourcesBound = false;
};
