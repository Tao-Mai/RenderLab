#pragma once

#include "render/device/ResourceBarriers.h"
#include "render/graph/RenderPass.h"

#include "render/device/Device.h"

#include "render/resources/ShaderManager.h"
#include "render/device/VulkanResources.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class GpuMesh;
namespace Falcor
{
class Device;
}  // namespace Falcor
struct DrawItem;

class EditorPickingPass : public Falcor::RenderPass
{
    FALCOR_PLUGIN_CLASS(EditorPickingPass, "EditorPickingPass", "Renders object IDs for picking.");

    EditorPickingPass(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties) : Falcor::RenderPass(std::move(device))
    {
        setProperties(properties);
    }

    static Falcor::ref<EditorPickingPass> create(Falcor::ref<Falcor::Device> device, const Falcor::Properties& properties)
    {
        return Falcor::make_ref<EditorPickingPass>(std::move(device), properties);
    }

    [[nodiscard]] Falcor::RenderPassReflection reflect(const CompileData& compileData) override;
    void                               compile(Falcor::RenderContext* passContext, const CompileData& compileData) override;
    void execute(Falcor::RenderContext* renderCommands, const Falcor::RenderData& renderData) override;

private:
    ResourceBarriers barriers;
    void                     draw(const Falcor::RenderContext& context, const DrawItem& item) const;
    VulkanResources::ImageHandle depth;
    VulkanResources::ImageHandle pickingImage;
    ShaderHandle             shader;
    vk::PipelineLayout       pipelineLayout;
    vk::Pipeline             pipeline;
};
