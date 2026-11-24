#pragma once
#include <slang-rhi.h>

#include "render/graph/RenderPass.h"

#include <slang-rhi/shader-cursor.h>
#include "render/raster/MeshDraw.h"

#include <array>

class GBufferPass : public RenderPass
{
    RL_PLUGIN_CLASS(GBufferPass, "GBufferPass", "Rasterizes the scene into G-buffer textures.");

    GBufferPass(rhi::ComPtr<rhi::IDevice> device, const Properties& properties) :
        RenderPass(std::move(device), SceneInputs::RasterVisibility)
    {
        setProperties(properties);
    }

    static ref<GBufferPass> create(rhi::ComPtr<rhi::IDevice> device, const Properties& properties)
    {
        return make_ref<GBufferPass>(std::move(device), properties);
    }

    [[nodiscard]] RenderPassReflection reflect(const CompileData& compileData) override;
    void compile(RenderContext* passContext, const CompileData& compileData) override;
    void execute(RenderContext* renderCommands, const RenderData& renderData) override;

private:
    static constexpr std::array<std::string_view, 5> kColorNames{
        "diffuseOcclusion",
        "normalRoughness",
        "emissiveSpecularF90",
        "specularF0DiffuseFresnel",
        "sheenColorRoughness",
    };
    static constexpr std::array kColorFormats{
        rhi::Format::RGBA8UnormSrgb,
        rhi::Format::RGBA16Float,
        rhi::Format::RGBA16Float,
        rhi::Format::RGBA16Float,
        rhi::Format::RGBA16Float,
    };

    MeshCullPipelines pipelines;
};
