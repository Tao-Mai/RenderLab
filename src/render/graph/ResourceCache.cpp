#include "render/graph/ResourceCache.h"

#include "core/Logger.h"
#include "render/device/RhiCheck.h"

namespace
{
void mergeResourceDesc(
    ResourceCache::ResourceDesc& source, const ResourceCache::ResourceDesc& target,
    const std::string& sourceName, const std::string& targetName
)
{
    if (source.index() != target.index())
    {
        LOG_ERROR("cannot connect '{}' to '{}': incompatible resource types", sourceName, targetName);
        return;
    }

    const auto merge = [&]<class T>(T& value, T requirement, const char* property)
    {
        if (requirement == T{})
            return;
        if (value != T{} && value != requirement)
            LOG_ERROR("cannot connect '{}' to '{}': incompatible {}", sourceName, targetName, property);
        else
            value = requirement;
    };

    if (auto* buffer = std::get_if<rhi::BufferDesc>(&source))
    {
        const auto& input = std::get<rhi::BufferDesc>(target);
        merge(buffer->size, input.size, "buffer size");
        merge(buffer->elementSize, input.elementSize, "element size");
        merge(buffer->format, input.format, "buffer format");
        buffer->usage |= input.usage;
        return;
    }

    auto& texture = std::get<rhi::TextureDesc>(source);
    const auto& input = std::get<rhi::TextureDesc>(target);
    if (rhi::getTextureDimension(texture.type) != rhi::getTextureDimension(input.type))
        LOG_ERROR("cannot connect '{}' to '{}': incompatible texture dimensions", sourceName, targetName);
    merge(texture.size.width, input.size.width, "width");
    merge(texture.size.height, input.size.height, "height");
    merge(texture.size.depth, input.size.depth, "depth");
    merge(texture.format, input.format, "texture format");
    merge(texture.sampleCount, input.sampleCount, "sample count");
    merge(texture.mipCount, input.mipCount, "mip count");
    merge(texture.arrayLength, input.arrayLength, "array length");
    texture.usage |= input.usage;
}
}  // namespace

void ResourceCache::addResource(std::string name, const ResourceDesc& desc, rhi::IResource* resource, bool allocate)
{
    indices.emplace(name, slots.size());
    slots.push_back({desc, rhi::ComPtr<rhi::IResource>{resource}, std::move(name), allocate});
}

void ResourceCache::connect(std::string name, const std::string& sourceName, const ResourceDesc& desc)
{
    const auto index = indices.at(sourceName);
    mergeResourceDesc(slots[index].resourceDesc, desc, sourceName, name);
    indices.emplace(std::move(name), index);
}

void ResourceCache::markOutput(const std::string& name)
{
    auto& desc = slots[indices.at(name)].resourceDesc;
    if (auto* texture = std::get_if<rhi::TextureDesc>(&desc))
        texture->usage |= rhi::TextureUsage::ShaderResource;
}

void ResourceCache::allocateResources(rhi::IDevice& device, const DefaultProperties& defaults)
{
    for (auto& slot : slots)
    {
        if (!slot.allocate)
            continue;

        if (auto* buffer = std::get_if<rhi::BufferDesc>(&slot.resourceDesc))
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
        desc.size.width = desc.size.width ? desc.size.width : defaults.dims.x;
        desc.size.height = desc.size.height ? desc.size.height : defaults.dims.y;
        desc.format = desc.format == rhi::Format::Undefined ? defaults.format : desc.format;
        CHECK(desc.size.width && desc.size.height && desc.format != rhi::Format::Undefined,
            "invalid graph texture '{}'", slot.name);
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
