#pragma once

#include <slang-rhi.h>
#include "core/Macros.h"
#include "render/graph/RenderGraphFlags.h"

#include <string>
#include <vector>

namespace Falcor
{
class FALCOR_API RenderPassReflection
{
public:
    class FALCOR_API Field
    {
    public:
        enum class Visibility
        {
            Undefined = 0x0,
            Input     = 0x1,
            Output    = 0x2,
            Internal  = 0x4,
        };

        enum class Flags
        {
            None       = 0x0,
            Optional   = 0x1,
        };

        enum class Type
        {
            Texture1D,
            Texture2D,
            Texture3D,
            TextureCube,
            RawBuffer,
        };

        static constexpr uint32_t kMaxMipLevels = ~0u;

        Field() = default;
        Field(const std::string& name, const std::string& desc, Visibility visibility);

        [[nodiscard]] bool isValid() const;
        Field&             rawBuffer(uint32_t size);
        Field&             texture1D(uint32_t width = 0, uint32_t mipCount = 1, uint32_t arraySize = 1);
        Field&             texture2D(
                        uint32_t width       = 0,
                        uint32_t height      = 0,
                        uint32_t sampleCount = 1,
                        uint32_t mipCount    = 1,
                        uint32_t arraySize   = 1
                    );
        Field& texture3D(uint32_t width = 0, uint32_t height = 0, uint32_t depth = 0, uint32_t arraySize = 1);
        Field& textureCube(uint32_t width = 0, uint32_t height = 0, uint32_t mipCount = 1, uint32_t arraySize = 1);
        Field& resourceType(
            Type     type,
            uint32_t width,
            uint32_t height,
            uint32_t depth,
            uint32_t sampleCount,
            uint32_t mipCount,
            uint32_t arraySize
        );

        Field& format(rhi::Format format);
        Field& bindFlags(ResourceBindFlags flags);
        Field& flags(Flags flags);
        Field& visibility(Visibility visibility);
        Field& name(const std::string& name);
        Field& desc(const std::string& desc);

        [[nodiscard]] const std::string& getName() const { return mName; }
        [[nodiscard]] const std::string& getDesc() const { return mDesc; }
        [[nodiscard]] uint32_t           getWidth() const { return mWidth; }
        [[nodiscard]] uint32_t           getHeight() const { return mHeight; }
        [[nodiscard]] uint32_t           getDepth() const { return mDepth; }
        [[nodiscard]] uint32_t           getSampleCount() const { return mSampleCount; }
        [[nodiscard]] uint32_t           getArraySize() const { return mArraySize; }
        [[nodiscard]] uint32_t           getMipCount() const { return mMipCount; }
        [[nodiscard]] rhi::Format     getFormat() const { return mFormat; }
        [[nodiscard]] ResourceBindFlags  getBindFlags() const { return mBindFlags; }
        [[nodiscard]] Flags              getFlags() const { return mFlags; }
        [[nodiscard]] Type               getType() const { return mType; }
        [[nodiscard]] Visibility         getVisibility() const { return mVisibility; }

        Field& merge(const Field& other);
        bool   operator==(const Field&) const = default;
        bool   operator!=(const Field& other) const { return !(*this == other); }

    private:
        Type              mType = Type::Texture2D;
        std::string       mName;
        std::string       mDesc;
        uint32_t          mWidth       = 0;
        uint32_t          mHeight      = 0;
        uint32_t          mDepth       = 0;
        uint32_t          mSampleCount = 1;
        uint32_t          mMipCount    = 1;
        uint32_t          mArraySize   = 1;
        rhi::Format    mFormat      = rhi::Format::Undefined;
        ResourceBindFlags mBindFlags   = ResourceBindFlags::None;
        Flags             mFlags       = Flags::None;
        Visibility        mVisibility  = Visibility::Undefined;
    };

    Field& addInput(const std::string& name, const std::string& desc);
    Field& addOutput(const std::string& name, const std::string& desc);
    Field& addInputOutput(const std::string& name, const std::string& desc);
    Field& addInternal(const std::string& name, const std::string& desc);

    [[nodiscard]] size_t       getFieldCount() const { return mFields.size(); }
    [[nodiscard]] const Field* getField(size_t index) const
    {
        return index < mFields.size() ? &mFields[index] : nullptr;
    }
    [[nodiscard]] const Field* getField(const std::string& name) const;
    [[nodiscard]] Field*       getField(const std::string& name);
    Field&                     addField(const Field& field);

    bool operator==(const RenderPassReflection& other) const;
    bool operator!=(const RenderPassReflection& other) const { return !(*this == other); }

private:
    Field& addField(const std::string& name, const std::string& desc, Field::Visibility visibility);

    std::vector<Field> mFields;
};

FALCOR_ENUM_CLASS_OPERATORS(RenderPassReflection::Field::Visibility);
FALCOR_ENUM_CLASS_OPERATORS(RenderPassReflection::Field::Flags);

[[nodiscard]] std::string                       to_string(RenderPassReflection::Field::Type type);
[[nodiscard]] RenderPassReflection::Field::Type resourceTypeToFieldType(rhi::TextureType type);
}  // namespace Falcor
