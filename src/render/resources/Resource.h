#pragma once

#include "core/Object.h"
#include "render/resources/Formats.h"
#include "render/resources/ResourceViews.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace Falcor
{
class Buffer;
class Device;
class Texture;

class FALCOR_API Resource : public Object
{
    FALCOR_OBJECT(Resource)

public:
    static constexpr uint32_t kMaxPossible = ResourceView::kMaxPossible;

    enum class Type
    {
        Buffer,
        Texture1D,
        Texture2D,
        Texture3D,
        TextureCube,
        Texture2DMultisample,
    };

    ~Resource() override = 0;

    [[nodiscard]] ref<Device> getDevice() const;
    [[nodiscard]] ResourceBindFlags getBindFlags() const { return mBindFlags; }

    struct ViewInfoHashFunc
    {
        std::size_t operator()(const ResourceViewInfo& view) const
        {
            const auto hash32 = std::hash<uint32_t>{};
            const auto hash64 = std::hash<uint64_t>{};
            return ((hash32(view.firstArraySlice) ^ (hash32(view.arraySize) << 1)) >> 1) ^
                   (hash32(view.mipCount) << 1) ^ (hash32(view.mostDetailedMip) << 3) ^
                   (hash64(view.offset) << 5) ^ (hash64(view.size) << 7);
        }
    };

    [[nodiscard]] Type getType() const { return mType; }
    [[nodiscard]] NativeHandle getNativeHandle() const;
    [[nodiscard]] size_t getSize() const { return mSize; }
    void invalidateViews() const;
    void setName(const std::string& name);
    [[nodiscard]] const std::string& getName() const { return mName; }
    [[nodiscard]] virtual ref<UnorderedAccessView> getUAV() = 0;
    [[nodiscard]] ref<Texture> asTexture();
    [[nodiscard]] ref<Buffer>  asBuffer();
    void breakStrongReferenceToDevice();

protected:
    Resource(ref<Device> device, Type type, ResourceBindFlags bindFlags, uint64_t size);
    Resource(Resource&& other) noexcept;
    Resource& operator=(Resource&& other) noexcept;

    BreakableReference<Device> mpDevice;
    Type mType;
    ResourceBindFlags mBindFlags;
    size_t mSize;
    std::string mName;
    mutable std::unordered_map<ResourceViewInfo, ref<UnorderedAccessView>, ViewInfoHashFunc> mUavs;
};
}  // namespace Falcor
