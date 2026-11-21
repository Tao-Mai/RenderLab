#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "render/raster/MeshDraw.h"
#include "render/raster/RasterAttachments.h"

#include <array>

class GBufferPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(GBufferPass, "GBufferPass", "Rasterizes the scene into G-buffer textures.");

    GBufferPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) : Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<GBufferPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<GBufferPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ResourceBarriers barriers;
    static constexpr std::array<std::string_view, 5> kColorNames{
        "diffuseOcclusion",
        "normalRoughness",
        "emissiveSpecularF90",
        "specularF0DiffuseFresnel",
        "sheenColorRoughness",
    };
    static constexpr std::array kColorFormats{
        vk::Format::eR8G8B8A8Srgb,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Format::eR16G16B16A16Sfloat,
        vk::Format::eR16G16B16A16Sfloat,
    };
    RasterAttachments<kColorFormats.size()> attachments;
    MeshCullPipelines                       pipelines;
};
