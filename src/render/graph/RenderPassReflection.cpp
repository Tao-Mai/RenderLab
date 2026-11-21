#include "render/graph/RenderPassReflection.h"

#include "core/Logger.h"

#include <algorithm>

namespace Falcor
{
RenderPassReflection::Field::Field(const std::string& name, const std::string& desc, Visibility visibility) :
    mName(name), mDesc(desc), mVisibility(visibility)
{
}

bool RenderPassReflection::Field::isValid() const
{
    return (mSampleCount <= 1 || mMipCount <= 1) &&
        !(is_set(mVisibility, Visibility::Internal) && is_set(mFlags, Flags::Optional));
}

RenderPassReflection::Field& RenderPassReflection::Field::rawBuffer(uint32_t size)
{
    mType   = Type::RawBuffer;
    mWidth  = size;
    mHeight = mDepth = mArraySize = mMipCount = 0;
    return *this;
}

RenderPassReflection::Field&
RenderPassReflection::Field::texture1D(uint32_t width, uint32_t mipCount, uint32_t arraySize)
{
    return resourceType(Type::Texture1D, width, 1, 1, 1, mipCount, arraySize);
}

RenderPassReflection::Field& RenderPassReflection::Field::texture2D(
    uint32_t width, uint32_t height, uint32_t sampleCount, uint32_t mipCount, uint32_t arraySize
)
{
    return resourceType(Type::Texture2D, width, height, 1, sampleCount, mipCount, arraySize);
}

RenderPassReflection::Field&
RenderPassReflection::Field::texture3D(uint32_t width, uint32_t height, uint32_t depth, uint32_t arraySize)
{
    return resourceType(Type::Texture3D, width, height, depth, 1, 1, arraySize);
}

RenderPassReflection::Field&
RenderPassReflection::Field::textureCube(uint32_t width, uint32_t height, uint32_t mipCount, uint32_t arraySize)
{
    return resourceType(Type::TextureCube, width, height, 1, 1, mipCount, arraySize);
}

RenderPassReflection::Field& RenderPassReflection::Field::resourceType(
    Type     value,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t sampleCount,
    uint32_t mipCount,
    uint32_t arraySize
)
{
    mType        = value;
    mWidth       = width;
    mHeight      = height;
    mDepth       = depth;
    mSampleCount = sampleCount;
    mMipCount    = mipCount;
    mArraySize   = arraySize;
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::Field::format(ResourceFormat value)
{
    mFormat = value;
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::Field::bindFlags(ResourceBindFlags value)
{
    mBindFlags = value;
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::Field::flags(Flags value)
{
    mFlags = value;
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::Field::visibility(Visibility value)
{
    mVisibility = value;
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::Field::name(const std::string& value)
{
    mName = value;
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::Field::desc(const std::string& value)
{
    mDesc = value;
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::Field::merge(const Field& other)
{
    DCHECK(!is_set(mVisibility | other.mVisibility, Visibility::Internal));

    const auto reportMismatch = [&](const char* property)
    {
        const auto message = std::format("incompatible {} for '{}' and '{}'", property, mName, other.mName);
        CHECK(logger::ScopedErrorLog::isActive(), "{}", message);
        LOG_ERROR("{}", message);
    };

    if (mType != other.mType)
    {
        reportMismatch("resource types");
        return *this;
    }

    bool compatible = true;
    const auto mergeValue = [&](auto value, auto incoming, const char* property)
    {
        if (incoming == decltype(incoming){})
            return value;
        if (value != decltype(value){} && value != incoming)
        {
            reportMismatch(property);
            compatible = false;
        }
        return incoming;
    };

    const auto width       = mergeValue(mWidth, other.mWidth, "width");
    const auto height      = mergeValue(mHeight, other.mHeight, "height");
    const auto depth       = mergeValue(mDepth, other.mDepth, "depth");
    const auto sampleCount = mergeValue(mSampleCount, other.mSampleCount, "sample count");
    const auto mipCount    = mergeValue(mMipCount, other.mMipCount, "mip count");
    const auto arraySize   = mergeValue(mArraySize, other.mArraySize, "array size");
    const auto format      = mergeValue(mFormat, other.mFormat, "format");
    if (compatible)
    {
        resourceType(mType, width, height, depth, sampleCount, mipCount, arraySize);
        mFormat     = format;
        mVisibility = mVisibility | other.mVisibility;
        mBindFlags  = mBindFlags | other.mBindFlags;
    }
    return *this;
}

RenderPassReflection::Field& RenderPassReflection::addInput(const std::string& name, const std::string& desc)
{
    return addField(name, desc, Field::Visibility::Input);
}

RenderPassReflection::Field& RenderPassReflection::addOutput(const std::string& name, const std::string& desc)
{
    return addField(name, desc, Field::Visibility::Output);
}

RenderPassReflection::Field& RenderPassReflection::addInputOutput(const std::string& name, const std::string& desc)
{
    return addField(name, desc, Field::Visibility::Input | Field::Visibility::Output);
}

RenderPassReflection::Field& RenderPassReflection::addInternal(const std::string& name, const std::string& desc)
{
    return addField(name, desc, Field::Visibility::Internal);
}

const RenderPassReflection::Field* RenderPassReflection::getField(const std::string& name) const
{
    const auto found = std::ranges::find(mFields, name, &Field::getName);
    return found == mFields.end() ? nullptr : &*found;
}

RenderPassReflection::Field* RenderPassReflection::getField(const std::string& name)
{
    const auto found = std::ranges::find(mFields, name, &Field::getName);
    return found == mFields.end() ? nullptr : &*found;
}

RenderPassReflection::Field& RenderPassReflection::addField(const Field& field)
{
    if (auto* existing = getField(field.getName()))
    {
        constexpr auto kInputOutput = Field::Visibility::Input | Field::Visibility::Output;
        if (is_set(existing->getVisibility(), kInputOutput) && is_set(field.getVisibility(), kInputOutput))
            existing->visibility(existing->getVisibility() | field.getVisibility());
        return *existing;
    }
    return mFields.emplace_back(field);
}

RenderPassReflection::Field&
RenderPassReflection::addField(const std::string& name, const std::string& desc, Field::Visibility visibility)
{
    DCHECK(!name.empty() && name.find('.') == std::string::npos);
    return addField(Field{name, desc, visibility});
}

bool RenderPassReflection::operator==(const RenderPassReflection& other) const
{
    if (mFields.size() != other.mFields.size())
        return false;
    return std::ranges::all_of(
        mFields,
        [&](const auto& field)
        {
            const auto* otherField = other.getField(field.getName());
            return otherField && *otherField == field;
        }
    );
}

std::string to_string(RenderPassReflection::Field::Type type)
{
    using Type = RenderPassReflection::Field::Type;
    switch (type)
    {
        case Type::Texture1D:
            return "Texture1D";
        case Type::Texture2D:
            return "Texture2D";
        case Type::Texture3D:
            return "Texture3D";
        case Type::TextureCube:
            return "TextureCube";
        case Type::RawBuffer:
            return "RawBuffer";
    }
    CHECK(false);
    return {};
}

RenderPassReflection::Field::Type resourceTypeToFieldType(Resource::Type type)
{
    using Type = RenderPassReflection::Field::Type;
    switch (type)
    {
        case Resource::Type::Texture1D:
            return Type::Texture1D;
        case Resource::Type::Texture2D:
        case Resource::Type::Texture2DMultisample:
            return Type::Texture2D;
        case Resource::Type::Texture3D:
            return Type::Texture3D;
        case Resource::Type::TextureCube:
            return Type::TextureCube;
        case Resource::Type::Buffer:
            return Type::RawBuffer;
    }
    CHECK(false);
    return Type::RawBuffer;
}
}  // namespace Falcor
