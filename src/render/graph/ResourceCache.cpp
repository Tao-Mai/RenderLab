#include "render/graph/ResourceCache.h"

#include <slang-rhi.h>
#include "core/Logger.h"

#include <algorithm>
#include <bit>
#include <utility>
#include "render/device/RhiCheck.h"

namespace
{
    using Field = RenderPassReflection::Field;
    using Type  = Field::Type;


    rhi::ComPtr<rhi::IResource> createResourceForPass(
        rhi::IDevice*                           device,
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
                flags |= ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess;
            rhi::BufferDesc desc;
            desc.size  = width;
            desc.usage = rhi::BufferUsage::CopySource | rhi::BufferUsage::CopyDestination;
            if (is_set(flags, ResourceBindFlags::ShaderResource))
                desc.usage |= rhi::BufferUsage::ShaderResource;
            if (is_set(flags, ResourceBindFlags::UnorderedAccess))
                desc.usage |= rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::ShaderResource;
            if (is_set(flags, ResourceBindFlags::Constant))
                desc.usage |= rhi::BufferUsage::ConstantBuffer;
            if (is_set(flags, ResourceBindFlags::Vertex))
                desc.usage |= rhi::BufferUsage::VertexBuffer;
            if (is_set(flags, ResourceBindFlags::Index))
                desc.usage |= rhi::BufferUsage::IndexBuffer;
            if (is_set(flags, ResourceBindFlags::IndirectArg))
                desc.usage |= rhi::BufferUsage::IndirectArgument;
            desc.label = name.c_str();
            rhi::ComPtr<rhi::IBuffer> buffer;
            rhiCheck(device->createBuffer(desc, nullptr, buffer.writeRef()));
            return rhi::ComPtr<rhi::IResource>(buffer.get());
        }

        const auto height    = field.getType() == Type::Texture1D ? 1u
               : field.getHeight()                                ? field.getHeight()
                                                                  : params.dims.y;
        const auto depth     = std::max(1u, field.getDepth());
        const auto samples   = std::max(1u, field.getSampleCount());
        const auto arraySize = std::max(1u, field.getArraySize());
        const auto mipCount  = field.getMipCount() == Field::kMaxMipLevels
             ? uint32_t(std::bit_width(std::max({width, height, depth})))
             : std::max(1u, field.getMipCount());
        const auto format    = field.getFormat() == rhi::Format::Undefined ? params.format : field.getFormat();
        CHECK(width && height && format != rhi::Format::Undefined, "invalid texture properties for '{}'", name);
        if (resolveBindFlags)
        {
            rhi::FormatSupport support;
            rhiCheck(device->getFormatSupport(format, &support));
            if (rhi::is_set(support, rhi::FormatSupport::ShaderSample))
                flags |= ResourceBindFlags::ShaderResource;
            if (samples == 1 && rhi::is_set(support, rhi::FormatSupport::ShaderUavStore))
                flags |= ResourceBindFlags::UnorderedAccess;
            if (is_set(field.getVisibility(), Field::Visibility::Output | Field::Visibility::Internal))
            {
                if (rhi::is_set(support, rhi::FormatSupport::RenderTarget))
                    flags |= ResourceBindFlags::RenderTarget;
                if (rhi::is_set(support, rhi::FormatSupport::DepthStencil))
                    flags |= ResourceBindFlags::DepthStencil;
            }
        }
        rhi::TextureDesc desc;
        switch (field.getType())
        {
            case Type::Texture1D:
                desc.type = arraySize > 1 ? rhi::TextureType::Texture1DArray : rhi::TextureType::Texture1D;
                break;
            case Type::Texture2D:
                desc.type = samples > 1
                    ? (arraySize > 1 ? rhi::TextureType::Texture2DMSArray : rhi::TextureType::Texture2DMS)
                    : arraySize > 1 ? rhi::TextureType::Texture2DArray
                                    : rhi::TextureType::Texture2D;
                break;
            case Type::Texture3D:
                CHECK(arraySize == 1, "3D texture '{}' must have a single layer", name);
                desc.type = rhi::TextureType::Texture3D;
                break;
            case Type::TextureCube:
                desc.type = arraySize > 1 ? rhi::TextureType::TextureCubeArray : rhi::TextureType::TextureCube;
                break;
            case Type::RawBuffer:
                std::unreachable();
        }
        desc.size        = {width, height, depth};
        desc.format      = format;
        desc.arrayLength = arraySize;
        desc.mipCount    = mipCount;
        desc.sampleCount = samples;
        desc.usage       = rhi::TextureUsage::CopySource | rhi::TextureUsage::CopyDestination;
        if (is_set(flags, ResourceBindFlags::ShaderResource))
            desc.usage |= rhi::TextureUsage::ShaderResource;
        if (is_set(flags, ResourceBindFlags::UnorderedAccess))
            desc.usage |= rhi::TextureUsage::UnorderedAccess;
        if (is_set(flags, ResourceBindFlags::RenderTarget))
            desc.usage |= rhi::TextureUsage::RenderTarget;
        if (is_set(flags, ResourceBindFlags::DepthStencil))
            desc.usage |= rhi::TextureUsage::DepthStencil;
        desc.label = name.c_str();
        rhi::ComPtr<rhi::ITexture> texture;
        rhiCheck(device->createTexture(desc, nullptr, texture.writeRef()));
        return rhi::ComPtr<rhi::IResource>(texture.get());
    }
}  // namespace

void ResourceCache::registerExternalResource(const std::string& name, rhi::IResource* resource)
{
    if (const auto found = mNameToIndex.find(name); found != mNameToIndex.end())
        mResourceData[found->second].pResource = resource;

    if (!resource)
        mExternalResources.erase(name);
    else
        mExternalResources.insert_or_assign(name, resource);
}

void ResourceCache::registerField(const std::string& name, const Field& field, const std::string& alias)
{

    CHECK(field.isValid(), "invalid field '{}'", name);
    if (!alias.empty())
    {
        const auto found = mNameToIndex.find(alias);

        const auto index = found->second;
        auto&      data  = mResourceData[index];
        data.field.merge(field);
        if (logger::ScopedErrorLog::hasErrors())
        {
            LOG_ERROR("cannot connect '{}' to '{}'", alias, name);
            return;
        }

        data.resolveBindFlags |= field.getBindFlags() == ResourceBindFlags::None;
        mNameToIndex.emplace(name, index);
        return;
    }

    mNameToIndex.emplace(name, static_cast<uint32_t>(mResourceData.size()));
    const auto external = mExternalResources.find(name);
    const auto resource = external != mExternalResources.end() ? external->second : rhi::ComPtr<rhi::IResource>{};
    mResourceData.push_back(
        {field, resource, field.getBindFlags() == ResourceBindFlags::None, name}
    );
}

const rhi::ComPtr<rhi::IResource>& ResourceCache::getResource(const std::string& name) const
{
    static const rhi::ComPtr<rhi::IResource> kNullResource;
    if (const auto external = mExternalResources.find(name); external != mExternalResources.end())
        return external->second;

    const auto found = mNameToIndex.find(name);
    return found == mNameToIndex.end() ? kNullResource : mResourceData[found->second].pResource;
}

void ResourceCache::setResource(const std::string& name, rhi::IResource* resource)
{
    mResourceData[mNameToIndex.at(name)].pResource = resource;
    if (const auto external = mExternalResources.find(name); external != mExternalResources.end())
        external->second = resource;
}

void ResourceCache::allocateResources(rhi::IDevice* device, const DefaultProperties& params)
{
    for (auto& data : mResourceData)
    {
        if (!data.pResource)
        {
            data.pResource = createResourceForPass(device, params, data.field, data.resolveBindFlags, data.name);
        }
    }
}
