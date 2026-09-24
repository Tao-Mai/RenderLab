#include "render/resource/Mesh.h"

#include "render/device/VkCheck.h"

#include <array>
#include <utility>
#include "core/Logger.h"

Mesh::Mesh(GpuUploadContext upload, MeshGeometry geometry, std::vector<Submesh> submeshes) :
    indexTotal(static_cast<uint32_t>(geometry.indices.size())),
    parts(std::move(submeshes))
{
    CHECK(!geometry.vertices.empty() && !geometry.indices.empty() && !parts.empty(),
        "mesh requires geometry and submeshes");
    for (uint32_t index : geometry.indices)
    {
        CHECK(index < geometry.vertices.size(), "mesh index references a missing vertex");
    }
    for (const Submesh& part : parts)
    {
        CHECK(part.firstIndex <= indexTotal &&
              part.indexCount <= indexTotal - part.firstIndex,
            "mesh submesh range is invalid");
    }

    const auto& physicalDevice = upload.physicalDevice;
    const auto& device = upload.device;
    const auto& commandPool = upload.commandPool;
    auto& queue = upload.queue;

    const vk::DeviceSize vertexBytes = sizeof(Vertex) * geometry.vertices.size();
    Buffer vertexStaging(
        physicalDevice, device, vertexBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    vertexStaging.upload(geometry.vertices.data(), vertexBytes);
    vertexBuffer = Buffer(
        physicalDevice, device, vertexBytes,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(device, commandPool, queue, vertexStaging, vertexBuffer);

    const vk::DeviceSize indexBytes = sizeof(uint32_t) * geometry.indices.size();
    Buffer indexStaging(
        physicalDevice, device, indexBytes,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    indexStaging.upload(geometry.indices.data(), indexBytes);
    indexBuffer = Buffer(
        physicalDevice, device, indexBytes,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    copyBuffer(device, commandPool, queue, indexStaging, indexBuffer);
}

void Mesh::bind(vk::raii::CommandBuffer& commandBuffer) const
{
    const std::array<vk::Buffer, 1> vertexBuffers{vertexBuffer.handle()};
    const std::array<vk::DeviceSize, 1> vertexOffsets{0};
    commandBuffer.bindVertexBuffers(0, vertexBuffers, vertexOffsets);
    commandBuffer.bindIndexBuffer(indexBuffer.handle(), 0, vk::IndexType::eUint32);
}

uint32_t Mesh::indexCount() const
{
    return indexTotal;
}

const std::vector<Submesh>& Mesh::submeshes() const
{
    return parts;
}

void Mesh::copyBuffer(
    const vk::raii::Device& device,
    const vk::raii::CommandPool& commandPool,
    vk::raii::Queue& queue,
    const Buffer& source,
    const Buffer& destination)
{
    const vk::CommandBufferAllocateInfo allocationInfo{
        .commandPool = commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    vk::raii::CommandBuffer copyCommand = std::move(vkCheck(
        device.allocateCommandBuffers(allocationInfo)).front());
    vkCheck(
        copyCommand.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
    copyCommand.copyBuffer(
        source.handle(), destination.handle(), vk::BufferCopy{.size = source.size()});
    vkCheck(copyCommand.end());
    const vk::CommandBuffer command = *copyCommand;
    const vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    vkCheck(queue.submit(submitInfo, nullptr));
    vkCheck(queue.waitIdle());
}
