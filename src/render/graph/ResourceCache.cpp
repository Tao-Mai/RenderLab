#include "render/graph/ResourceCache.h"

#include "core/Logger.h"
#include "render/device/Device.h"
#include "render/resources/Buffer.h"
#include "render/resources/Texture.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace Falcor
{
namespace
{
using Field = RenderPassReflection::Field;
using Type  = Field::Type;

ref<Resource> createResourceForPass(
    const ref<Device>&                      device,
    const ResourceCache::DefaultProperties& params,
    const Field&                            field,
    bool                                    resolveBindFlags,
    const std::string&                      name
)
{
    const auto width = field.getWidth() ? field.getWidth() : params.dims.x;
    auto       flags = field.getBindFlags();
    if (field.getType() == Type::RawBuffer)
    {
        CHECK(width, "buffer '{}' requires a size", name);
        if (resolveBindFlags)
            flags = flags | ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess;
        return device->createBuffer(width, flags, MemoryType::DeviceLocal);
    }

    const auto height = field.getType() == Type::Texture1D ? 1u : field.getHeight() ? field.getHeight() : params.dims.y;
    const auto depth  = std::max(1u, field.getDepth());
    const auto samples   = std::max(1u, field.getSampleCount());
    const auto arraySize = std::max(1u, field.getArraySize());
    const auto mipCount  = field.getMipCount() == Field::kMaxMipLevels
         ? static_cast<uint32_t>(std::bit_width(std::max({width, height, depth})))
         : std::max(1u, field.getMipCount());
    const auto format    = field.getFormat() == ResourceFormat::Unknown ? params.format : field.getFormat();
    CHECK(width && height && format != ResourceFormat::Unknown, "invalid texture properties for '{}'", name);
    if (resolveBindFlags)
    {
        auto mask = ResourceBindFlags::ShaderResource;
        if (samples == 1)
            mask = mask | ResourceBindFlags::UnorderedAccess;
        if (is_set(field.getVisibility(), Field::Visibility::Output | Field::Visibility::Internal))
            mask = mask | ResourceBindFlags::RenderTarget | ResourceBindFlags::DepthStencil;
        flags = flags | (mask & device->getFormatBindFlags(format));
    }

    switch (field.getType())
    {
        case Type::Texture1D:
            return device->createTexture1D(width, format, arraySize, mipCount, flags);
        case Type::Texture2D:
            if (samples > 1)
                return device->createTexture2DMS(width, height, format, samples, arraySize, flags);
            return device->createTexture2D(width, height, format, arraySize, mipCount, flags);
        case Type::Texture3D:
            CHECK(arraySize == 1, "3D texture '{}' must have a single layer", name);
            return device->createTexture3D(width, height, depth, format, mipCount, flags);
        case Type::TextureCube:
            return device->createTextureCube(width, height, format, arraySize, mipCount, flags);
        case Type::RawBuffer:
            break;
    }
    LOG_FATAL("unknown resource type for '{}'", name);
}
}  // namespace

void ResourceCache::reset()
{
    mNameToIndex.clear();
    mResourceData.clear();
}

void ResourceCache::registerExternalResource(const std::string& name, const ref<Resource>& resource)
{
    if (const auto found = mNameToIndex.find(name); found != mNameToIndex.end())
        mResourceData[found->second].pResource = resource;

    if (!resource)
        mExternalResources.erase(name);
    else
        mExternalResources.insert_or_assign(name, resource);
}

void ResourceCache::registerField(
    const std::string& name, const Field& field, uint32_t timePoint, const std::string& alias
)
{
    DCHECK(!mNameToIndex.contains(name));
    CHECK(field.isValid(), "invalid field '{}'", name);
    if (!alias.empty())
    {
        const auto found = mNameToIndex.find(alias);
        DCHECK(found != mNameToIndex.end());
        const auto index = found->second;
        auto&      data  = mResourceData[index];
        data.field.merge(field);
        if (logger::ScopedErrorLog::hasErrors())
        {
            LOG_ERROR("cannot connect '{}' to '{}'", alias, name);
            return;
        }

        data.resolveBindFlags |= field.getBindFlags() == ResourceBindFlags::None;
        data.lifetime.first  = std::min(timePoint, data.lifetime.first);
        data.lifetime.second = std::max(timePoint, data.lifetime.second);
        mNameToIndex.emplace(name, index);
        return;
    }

    mNameToIndex.emplace(name, static_cast<uint32_t>(mResourceData.size()));
    const auto external = mExternalResources.find(name);
    const auto resource = external != mExternalResources.end() ? external->second : ref<Resource>{};
    mResourceData.push_back(
        {field, {timePoint, timePoint}, resource, field.getBindFlags() == ResourceBindFlags::None, name}
    );
}

const ref<Resource>& ResourceCache::getResource(const std::string& name) const
{
    static const ref<Resource> kNullResource;
    if (const auto external = mExternalResources.find(name); external != mExternalResources.end())
        return external->second;

    const auto found = mNameToIndex.find(name);
    return found == mNameToIndex.end() ? kNullResource : mResourceData[found->second].pResource;
}

const Field& ResourceCache::getResourceReflection(const std::string& name) const
{
    return mResourceData[mNameToIndex.at(name)].field;
}

void ResourceCache::allocateResources(ref<Device> device, const DefaultProperties& params)
{
    for (auto& data : mResourceData)
    {
        if (!data.pResource)
        {
            data.pResource = createResourceForPass(device, params, data.field, data.resolveBindFlags, data.name);
            data.pResource->setName(data.name);
        }
    }
}
}  // namespace Falcor
