#pragma once

#include "render/ShaderManager.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <vulkan/vulkan_raii.hpp>

class GpuMesh;
class PipelineManager;
class RenderGraph;
struct DrawItem;

class EditorPickingPass
{
public:
    enum Input { Depth, PickingImage, Readback, InputCount };
    enum Output { ImageResult, ReadbackResult, OutputCount };

    void init(RenderGraph& graph);
    void registerPass(RenderGraph& graph);
    void setupPass(RenderGraph& graph);
    void executePass(RenderGraph& graph) const;
    [[nodiscard]] uint32_t readSelectionId(uint32_t frameIndex);

private:
    void draw(
        vk::raii::CommandBuffer& commandBuffer,
        const DrawItem& item) const;
    RenderGraph* graph = nullptr;
    std::vector<uint32_t> inputSlots;
    std::vector<uint32_t> outputSlots;
    PipelineManager* pipelines = nullptr;
    ShaderHandle shader;
    vk::PipelineLayout pipelineLayout;
    vk::Pipeline pipeline;
};
