#include "render/resources/Buffer.h"

#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "render/device/Device.h"
#include "render/device/GpuUploadContext.h"
#include "render/device/Memory.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"

#include <cstring>
#include <utility>

namespace Falcor
{
namespace
{
vk::BufferUsageFlags toVulkanBufferUsage(ResourceBindFlags flags)
{
    auto usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
    if (is_set(flags, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess))
        usage |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (is_set(flags, ResourceBindFlags::Constant))
        usage |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (is_set(flags, ResourceBindFlags::Vertex))
        usage |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (is_set(flags, ResourceBindFlags::Index))
        usage |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (is_set(flags, ResourceBindFlags::IndirectArg))
        usage |= vk::BufferUsageFlagBits::eIndirectBuffer;
    if (is_set(flags, ResourceBindFlags::StreamOutput))
        usage |= vk::BufferUsageFlagBits::eTransformFeedbackBufferEXT;
    if (is_set(flags, ResourceBindFlags::AccelerationStructure))
        usage |= vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
    return usage;
}

void copyBuffer(VulkanContext& vulkan, const Buffer& source, const Buffer& destination, const vk::BufferCopy& region)
{
    const auto pool = vkCheck(vulkan.deviceHandle().createCommandPool({
        .flags = vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = vulkan.graphicsQueueFamilyIndex(),
    }));
    GpuUploadContext{vulkan, pool}.copyBuffer(source, destination, region);
}
}  // namespace

Buffer::Buffer(ref<Device> device, size_t size, ResourceBindFlags bindFlags, MemoryType memoryType, const void* initData) :
    Buffer(*device->vulkan, size, toVulkanBufferUsage(bindFlags), memoryType, bindFlags)
{
    if (initData)
        setBlob(initData, 0, size);
}

Buffer::Buffer(
    const VulkanContext& vulkan,
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    MemoryType memoryType,
    ResourceBindFlags bindFlags
) :
    Resource(ref<Device>(vulkan.mpDevice), Type::Buffer, bindFlags, size), bufferUsage(usage), mMemoryType(memoryType)
{
    DCHECK(mpDevice);
    CHECK(size != 0);

    const auto& device = vulkan.deviceHandle();
    const vk::BufferCreateInfo bufferInfo{.size = size, .usage = usage, .sharingMode = vk::SharingMode::eExclusive};
    buffer = vkCheck(device.createBuffer(bufferInfo));

    const auto memoryProperties = memoryType == MemoryType::DeviceLocal
        ? vk::MemoryPropertyFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
        : vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    const vk::MemoryRequirements      requirements = buffer.getMemoryRequirements();
    const vk::MemoryAllocateFlagsInfo addressFlags{
        .flags = vk::MemoryAllocateFlagBits::eDeviceAddress,
    };
    const vk::MemoryAllocateInfo allocationInfo{
        .pNext           = (usage & vk::BufferUsageFlagBits::eShaderDeviceAddress) ? &addressFlags : nullptr,
        .allocationSize  = requirements.size,
        .memoryTypeIndex = vulkan_memory::findType(vulkan.physicalDeviceHandle(), requirements.memoryTypeBits, memoryProperties)
    };

    memory = vkCheck(device.allocateMemory(allocationInfo));
    vkCheck(buffer.bindMemory(*memory, 0));
}

Buffer::~Buffer()
{
    invalidateViews();
    unmap();
}

Buffer::Buffer(Buffer&& other) noexcept : Buffer() { *this = std::move(other); }

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this == &other)
        return *this;

    unmap();
    Resource::operator=(std::move(other));
    bufferUsage = std::exchange(other.bufferUsage, {});
    mMemoryType = std::exchange(other.mMemoryType, MemoryType::DeviceLocal);
    mMappedPtr = std::exchange(other.mMappedPtr, nullptr);
    buffer = std::move(other.buffer);
    memory = std::move(other.memory);
    return *this;
}

ref<UnorderedAccessView> Buffer::getUAV() { return getUAV(0); }

ref<UnorderedAccessView> Buffer::getUAV(uint64_t offset, uint64_t size)
{
    const ResourceViewInfo info{offset, size};
    if (const auto view = mUavs.find(info); view != mUavs.end())
        return view->second;

    auto view = UnorderedAccessView::create(mpDevice.get(), this, offset, size);
    mUavs.emplace(info, view);
    return view;
}

void Buffer::setBlob(const void* data, size_t offset, size_t size)
{
    CHECK(offset <= mSize && size <= mSize - offset);
    CHECK(mMemoryType != MemoryType::ReadBack);
    if (!size)
        return;
    DCHECK(data);

    if (mMemoryType == MemoryType::Upload)
    {
        const bool mapped = mMappedPtr != nullptr;
        std::memcpy(static_cast<std::byte*>(map()) + offset, data, size);
        if (!mapped)
            unmap();
        invalidateViews();
        return;
    }

    CHECK(bufferUsage & vk::BufferUsageFlagBits::eTransferDst);
    auto& vulkan = *mpDevice->vulkan;
    Buffer staging(vulkan, size, vk::BufferUsageFlagBits::eTransferSrc, MemoryType::Upload, ResourceBindFlags::None);
    staging.setBlob(data, 0, size);
    copyBuffer(vulkan, staging, *this, {.dstOffset = offset, .size = size});
}

void* Buffer::map() const
{
    CHECK(mMemoryType == MemoryType::Upload || mMemoryType == MemoryType::ReadBack);
    if (!mMappedPtr)
        mMappedPtr = vkCheck(memory.mapMemory(0, mSize));
    return mMappedPtr;
}

void Buffer::unmap() const
{
    if (mMappedPtr)
    {
        memory.unmapMemory();
        mMappedPtr = nullptr;
    }
}

void Buffer::getBlob(void* data, size_t offset, size_t size) const
{
    CHECK(offset <= mSize && size <= mSize - offset);
    CHECK(mMemoryType != MemoryType::Upload);
    if (!size)
        return;
    DCHECK(data);

    if (mMemoryType == MemoryType::ReadBack)
    {
        const bool mapped = mMappedPtr != nullptr;
        std::memcpy(data, static_cast<const std::byte*>(map()) + offset, size);
        if (!mapped)
            unmap();
        return;
    }

    CHECK(bufferUsage & vk::BufferUsageFlagBits::eTransferSrc);
    auto& vulkan = *mpDevice->vulkan;
    const Buffer staging(vulkan, size, vk::BufferUsageFlagBits::eTransferDst, MemoryType::ReadBack, ResourceBindFlags::None);
    copyBuffer(vulkan, *this, staging, {.srcOffset = offset, .size = size});
    staging.getBlob(data, 0, size);
}

void Buffer::reset() noexcept
{
    invalidateViews();
    unmap();
    buffer         = nullptr;
    memory         = nullptr;
    mpDevice       = BreakableReference<Device>(ref<Device>{});
    mBindFlags     = ResourceBindFlags::None;
    mSize          = 0;
    mName.clear();
    bufferUsage    = {};
    mMemoryType    = MemoryType::DeviceLocal;
}

uint64_t Buffer::getGpuAddress() const
{
    DCHECK(mpDevice && mpDevice->device);
    return mpDevice->device->getBufferAddress({.buffer = *buffer});
}

FALCOR_SCRIPT_BINDING(Buffer)
{
    using namespace Falcor::ScriptBindings;
    FALCOR_SCRIPT_BINDING_DEPENDENCY(Resource)

    static PyGetSetDef kProperties[]{
        {"memory_type", [](PyObject* object, void*)
        {
            const auto& buffer = static_cast<const Buffer&>(*valueOf<ref<Resource>>(object));
            return toPython(buffer.getMemoryType()).release();
        }},
        {"size", [](PyObject* object, void*)
        {
            const auto& buffer = static_cast<const Buffer&>(*valueOf<ref<Resource>>(object));
            return PyLong_FromSize_t(buffer.getSize());
        }},
        {},
    };
    static PyType_Slot kSlots[]{{Py_tp_getset, kProperties}, {}};
    static PyType_Spec kType{
        "falcor.Buffer", 0, 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION, kSlots,
    };

    auto* base = getType(typeid(ref<Resource>));
    DCHECK(base);
    const auto buffer = makePythonType<ref<Resource>>(m, kType, reinterpret_cast<PyObject*>(base));
    return buffer ? PyModule_AddObjectRef(m, "Buffer", buffer.get()) : -1;
}
}  // namespace Falcor
