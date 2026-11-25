#include "render/graph/ResourceCache.h"

#include "core/Logger.h"
#include "render/RhiUtils.h"

#include <tuple>

namespace
{
void mergeResourceDesc(
    ResourceCache::ResourceDesc&       source,
    const ResourceCache::ResourceDesc& target,
    const std::string&                 sourceName,
    const std::string&                 targetName,
    bool                               external
)
{
    const auto requireCompatible = [&](bool compatible, const char* property)
    {
        CHECK(compatible, "cannot connect '{}' to '{}': incompatible {}", sourceName, targetName, property);
    };
    requireCompatible(source.index() == target.index(), "resource types");

    const auto merge = [&]<class T>(T& value, T requirement, const char* property)
    {
        if (requirement == T{})
            return;

        requireCompatible(value == requirement || (!external && value == T{}), property);
        value = requirement;
    };

    if (auto* buffer = std::get_if<rhi::BufferDesc>(&source))
    {
        const auto& input = std::get<rhi::BufferDesc>(target);
        merge(buffer->size, input.size, "buffer size");
        merge(buffer->elementSize, input.elementSize, "element size");
        merge(buffer->format, input.format, "buffer format");
        if (external)
            requireCompatible((buffer->usage & input.usage) == input.usage, "buffer usage");
        else
            buffer->usage |= input.usage;

        return;
    }

    auto&       texture = std::get<rhi::TextureDesc>(source);
    const auto& input   = std::get<rhi::TextureDesc>(target);
    requireCompatible(texture.type == input.type, "texture types");
    merge(texture.size.width, input.size.width, "width");
    merge(texture.size.height, input.size.height, "height");
    merge(texture.size.depth, input.size.depth, "depth");
    merge(texture.format, input.format, "texture format");
    merge(texture.sampleCount, input.sampleCount, "sample count");
    merge(texture.sampleQuality, input.sampleQuality, "sample quality");
    merge(texture.mipCount, input.mipCount, "mip count");
    merge(texture.arrayLength, input.arrayLength, "array length");
    if (external)
        requireCompatible((texture.usage & input.usage) == input.usage, "texture usage");
    else
        texture.usage |= input.usage;
}

void fillTextureDefaults(rhi::TextureDesc& texture, const ResourceCache::DefaultProperties& defaults)
{
    texture.size.width  = texture.size.width ? texture.size.width : defaults.dims.x;
    texture.size.height = texture.size.height ? texture.size.height : defaults.dims.y;
    texture.format      = texture.format == rhi::Format::Undefined ? defaults.format : texture.format;
}
}  // namespace

std::optional<ResourceCache::ResourceDesc> ResourceCache::describe(rhi::IResource& resource)
{
    rhi::ComPtr<rhi::IBuffer> buffer;
    if (SLANG_SUCCEEDED(
            resource.queryInterface(rhi::IBuffer::getTypeGuid(), reinterpret_cast<void**>(buffer.writeRef()))
        ))
        return buffer->getDesc();

    rhi::ComPtr<rhi::ITexture> texture;
    if (SLANG_SUCCEEDED(
            resource.queryInterface(rhi::ITexture::getTypeGuid(), reinterpret_cast<void**>(texture.writeRef()))
        ))
        return texture->getDesc();
    return std::nullopt;
}

bool ResourceCache::sameDescription(const ResourceDesc& a, const ResourceDesc& b)
{
    if (a.index() != b.index())
        return false;

    if (const auto* lhs = std::get_if<rhi::BufferDesc>(&a))
    {
        const auto& rhs = std::get<rhi::BufferDesc>(b);
        return std::tie(lhs->size, lhs->elementSize, lhs->format, lhs->usage, lhs->memoryType) ==
            std::tie(rhs.size, rhs.elementSize, rhs.format, rhs.usage, rhs.memoryType);
    }

    const auto& lhs = std::get<rhi::TextureDesc>(a);
    const auto& rhs = std::get<rhi::TextureDesc>(b);
    return std::tie(
               lhs.type,
               lhs.size.width,
               lhs.size.height,
               lhs.size.depth,
               lhs.arrayLength,
               lhs.mipCount,
               lhs.sampleCount,
               lhs.sampleQuality,
               lhs.format,
               lhs.usage,
               lhs.memoryType
           ) ==
        std::tie(
               rhs.type,
               rhs.size.width,
               rhs.size.height,
               rhs.size.depth,
               rhs.arrayLength,
               rhs.mipCount,
               rhs.sampleCount,
               rhs.sampleQuality,
               rhs.format,
               rhs.usage,
               rhs.memoryType
        );
}

size_t ResourceCache::addResource(std::string name, const ResourceDesc& desc)
{
    const auto index = slots.size();
    indices.emplace(name, index);
    slots.push_back({desc, {}, std::move(name)});
    return index;
}

void ResourceCache::addInput(std::string name, const ResourceDesc& requirements, rhi::IResource* resource)
{
    auto desc = *describe(*resource);
    mergeResourceDesc(desc, requirements, name, name, true);
    indices.emplace(name, slots.size());
    slots.push_back({desc, rhi::ComPtr<rhi::IResource>{resource}, std::move(name), true});
}

void ResourceCache::connect(std::string name, const std::string& sourceName, const ResourceDesc& desc)
{
    const auto index = indices.at(sourceName);
    auto&      slot  = slots[index];
    mergeResourceDesc(slot.resourceDesc, desc, sourceName, name, slot.external);
    indices.emplace(std::move(name), index);
}

void ResourceCache::matchTexture(
    size_t target, size_t source, RenderPassReflection::TextureMatch match, const DefaultProperties& defaults
)
{
    using TextureMatch = RenderPassReflection::TextureMatch;

    auto& slot        = slots[target];
    auto texture      = std::get<rhi::TextureDesc>(slots[source].resourceDesc);
    auto requirements = std::get<rhi::TextureDesc>(slot.resourceDesc);
    fillTextureDefaults(texture, defaults);
    if (is_set(match, TextureMatch::Extent))
    {
        requirements.size.width = texture.size.width;
        requirements.size.height = texture.size.height;
    }
    if (is_set(match, TextureMatch::Format))
        requirements.format = texture.format;
    mergeResourceDesc(slot.resourceDesc, requirements, slots[source].name, slot.name, slot.external);
}

void ResourceCache::markOutput(const std::string& name)
{
    auto& slot = slots[indices.at(name)];
    if (auto* texture = std::get_if<rhi::TextureDesc>(&slot.resourceDesc); texture && !slot.external)
        texture->usage |= rhi::TextureUsage::ShaderResource;
}

void ResourceCache::applyTextureDefaults(const DefaultProperties& defaults)
{
    for (auto& slot : slots)
        if (auto* texture = std::get_if<rhi::TextureDesc>(&slot.resourceDesc))
            fillTextureDefaults(*texture, defaults);
}

void ResourceCache::allocateResources(rhi::IDevice& device)
{
    for (auto& slot : slots)
    {
        if (slot.external)
            continue;

        if (const auto* buffer = std::get_if<rhi::BufferDesc>(&slot.resourceDesc))
        {
            auto desc = *buffer;
            CHECK(desc.size, "graph buffer '{}' requires a size", slot.name);
            desc.label = slot.name.c_str();
            desc.usage |= rhi::BufferUsage::CopySource | rhi::BufferUsage::CopyDestination;
            if (rhi::is_set(desc.usage, rhi::BufferUsage::UnorderedAccess))
                desc.usage |= rhi::BufferUsage::ShaderResource;
            rhi::ComPtr<rhi::IBuffer> resource;
            rhiCheck(device.createBuffer(desc, nullptr, resource.writeRef()));
            slot.resource = resource.get();
            continue;
        }

        auto desc = std::get<rhi::TextureDesc>(slot.resourceDesc);
        CHECK(
            desc.size.width && desc.size.height && desc.format != rhi::Format::Undefined,
            "invalid graph texture '{}'",
            slot.name
        );
        desc.label = slot.name.c_str();
        desc.usage |= rhi::TextureUsage::CopySource | rhi::TextureUsage::CopyDestination;
        rhi::ComPtr<rhi::ITexture> resource;
        rhiCheck(device.createTexture(desc, nullptr, resource.writeRef()));
        slot.resource = resource.get();
    }
}

std::optional<size_t> ResourceCache::findSlot(const std::string& name) const
{
    const auto found = indices.find(name);
    if (found != indices.end())
        return found->second;
    return std::nullopt;
}
