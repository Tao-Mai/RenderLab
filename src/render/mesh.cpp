#include "render/mesh.h"

#include <array>
#include <stdexcept>
#include <utility>

Mesh::Mesh(
    const vk::raii::PhysicalDevice &physicalDevice,
    const vk::raii::Device &device,
    const vk::raii::CommandPool &commandPool,
    vk::raii::Queue &queue,
    std::vector<Vertex> vertices,
    std::vector<uint32_t> indices) :
    vertices(std::move(vertices)),
    indices(std::move(indices))
{
    if (this->vertices.empty() || this->indices.empty())
    {
        throw std::invalid_argument("mesh requires vertices and indices");
    }

    for (const uint32_t index : this->indices)
    {
        if (index >= this->vertices.size())
        {
            throw std::out_of_range("mesh index references a missing vertex");
        }
    }

    const vk::DeviceSize vertexBytes =
        sizeof(Vertex) * this->vertices.size();
    Buffer vertexStaging(
        physicalDevice,
        device,
        vertexBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
    vertexStaging.upload(this->vertices.data(), vertexBytes);

    vertexBuffer = Buffer(
        physicalDevice,
        device,
        vertexBytes,
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(device, commandPool, queue, vertexStaging, vertexBuffer);

    const vk::DeviceSize indexBytes =
        sizeof(uint32_t) * this->indices.size();
    Buffer indexStaging(
        physicalDevice,
        device,
        indexBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent);
    indexStaging.upload(this->indices.data(), indexBytes);

    indexBuffer = Buffer(
        physicalDevice,
        device,
        indexBytes,
        vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(device, commandPool, queue, indexStaging, indexBuffer);
}

void Mesh::bind(vk::raii::CommandBuffer &commandBuffer) const
{
    const std::array<vk::Buffer, 1> vertexBuffers = {vertexBuffer.handle()};
    const std::array<vk::DeviceSize, 1> vertexOffsets = {0};
    commandBuffer.bindVertexBuffers(0, vertexBuffers, vertexOffsets);
    commandBuffer.bindIndexBuffer(indexBuffer.handle(), 0, vk::IndexType::eUint32);
}

uint32_t Mesh::indexCount() const
{
    return static_cast<uint32_t>(indices.size());
}

void Mesh::copyBuffer(
    const vk::raii::Device &device,
    const vk::raii::CommandPool &commandPool,
    vk::raii::Queue &queue,
    const Buffer &source,
    const Buffer &destination)
{
    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };
    vk::raii::CommandBuffer copyCommand =
        std::move(vk::raii::CommandBuffers(device, allocationInfo).front());

    copyCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    copyCommand.copyBuffer(
        source.handle(),
        destination.handle(),
        vk::BufferCopy{.size = source.size()});
    copyCommand.end();

    const vk::CommandBuffer command = *copyCommand;
    const vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &command
    };
    queue.submit(submitInfo, nullptr);
    queue.waitIdle();
}
