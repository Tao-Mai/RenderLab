#pragma once

#include <cstdint>
#include <utility>
#include <vulkan/vulkan_raii.hpp>

struct DrawStatistics;

// Extends Vulkan's RAII command buffer only with draw statistics.
class CommandRecorder final : public vk::raii::CommandBuffer
{
public:
    CommandRecorder(std::nullptr_t) : vk::raii::CommandBuffer(nullptr) {}
    explicit CommandRecorder(vk::raii::CommandBuffer&& buffer) : vk::raii::CommandBuffer(std::move(buffer)) {}

    void setStatistics(DrawStatistics* target) { statistics = target; }

    // Triangle statistics follow the renderer's triangle-list topology.
    void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) const;
    void drawIndexed(
        uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance
    ) const;
    void addStatistics(DrawStatistics counts) const;

private:
    DrawStatistics* statistics = nullptr;
};
