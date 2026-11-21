#pragma once

#include "core/Macros.h"
#include "core/Object.h"
#include "render/resources/NativeHandle.h"

namespace Falcor
{
class Buffer;
class Device;
class Resource;
class Texture;

struct FALCOR_API ResourceViewInfo
{
    static constexpr uint32_t kMaxPossible = ~0u;
    static constexpr uint64_t kEntireBuffer = ~uint64_t{0};

    ResourceViewInfo() = default;
    ResourceViewInfo(uint32_t mostDetailedMip, uint32_t mipCount, uint32_t firstArraySlice, uint32_t arraySize) :
        mostDetailedMip(mostDetailedMip), mipCount(mipCount), firstArraySlice(firstArraySlice), arraySize(arraySize)
    {
    }
    ResourceViewInfo(uint64_t offset, uint64_t size) : offset(offset), size(size) {}

    uint32_t mostDetailedMip = 0;
    uint32_t mipCount        = kMaxPossible;
    uint32_t firstArraySlice = 0;
    uint32_t arraySize       = kMaxPossible;
    uint64_t offset          = 0;
    uint64_t size            = kEntireBuffer;

    bool operator==(const ResourceViewInfo&) const = default;
};

class FALCOR_API ResourceView : public Object
{
    FALCOR_OBJECT(ResourceView)

public:
    static const uint32_t kMaxPossible = ResourceViewInfo::kMaxPossible;
    static constexpr uint64_t kEntireBuffer = ResourceViewInfo::kEntireBuffer;

    [[nodiscard]] NativeHandle getNativeHandle() const { return mNativeHandle; }
    [[nodiscard]] const ResourceViewInfo& getViewInfo() const { return mViewInfo; }
    [[nodiscard]] Resource* getResource() const { return mpResource; }

protected:
    friend class Resource;

    ResourceView(Resource* resource, NativeHandle handle, const ResourceViewInfo& info) :
        mNativeHandle(handle), mViewInfo(info), mpResource(resource)
    {
    }

    void invalidate()
    {
        mNativeHandle = {};
        mpResource = nullptr;
    }

    NativeHandle mNativeHandle;
    ResourceViewInfo mViewInfo;
    Resource* mpResource;
};

class FALCOR_API UnorderedAccessView : public ResourceView
{
public:
    [[nodiscard]] static ref<UnorderedAccessView> create(
        Device* device, Texture* texture, uint32_t mipLevel, uint32_t firstArraySlice, uint32_t arraySize
    );
    [[nodiscard]] static ref<UnorderedAccessView> create(Device* device, Buffer* buffer, uint64_t offset, uint64_t size);

private:
    using ResourceView::ResourceView;
};
}  // namespace Falcor
