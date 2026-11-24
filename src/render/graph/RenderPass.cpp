#include "render/graph/RenderPass.h"

#include "core/Logger.h"
#include "render/graph/ResourceCache.h"

#include <utility>

RenderData::RenderData(
    const std::string& passName,
    ResourceCache&     resources,
    Dictionary&        dictionary,
    const glm::uvec2&       defaultTexDims
) :
    mName(passName),
    mResources(resources),
    mDictionary(dictionary),
    mDefaultTexDims(defaultTexDims)
{
}

const rhi::ComPtr<rhi::IResource>& RenderData::getResource(std::string_view fieldName) const
{
    std::string qualifiedName;
    qualifiedName.reserve(mName.size() + 1 + fieldName.size());
    qualifiedName.append(mName).append(".").append(fieldName);
    return mResources.getResource(qualifiedName);
}

rhi::ComPtr<rhi::ITexture> RenderData::getTexture(std::string_view name) const
{
    const auto& resource = getResource(name);
    return rhi::ComPtr<rhi::ITexture>(static_cast<rhi::ITexture*>(resource.get()));
}

rhi::ComPtr<rhi::IBuffer> RenderData::getBuffer(std::string_view name) const
{
    return rhi::ComPtr<rhi::IBuffer>(static_cast<rhi::IBuffer*>(getResource(name).get()));
}

void RenderData::setBuffer(std::string_view name, rhi::IBuffer* buffer) const
{
    mResources.setResource(mName + "." + std::string(name), buffer);
}

ref<RenderPass> RenderPass::create(
    std::string_view type, rhi::ComPtr<rhi::IDevice> device, const Properties& properties, PluginManager& plugins
)
{
    if (!plugins.hasClass<RenderPass>(type))
        plugins.loadPluginByName(type);

    auto pass = plugins.createClass<RenderPass>(type, std::move(device), properties);
    CHECK(pass, "cannot create render pass '{}'", type);
    return pass;
}

void RenderPass::setProperties(const Properties& values)
{
    CHECK(values.empty(), "pass has no configurable properties");
}
