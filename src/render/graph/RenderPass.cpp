#include "render/graph/RenderPass.h"

#include "core/Logger.h"
#include "render/graph/ResourceCache.h"

#include <utility>

namespace Falcor
{
RenderData::RenderData(
    const std::string& passName,
    ResourceCache&     resources,
    Dictionary&        dictionary,
    const uint2&       defaultTexDims,
    ResourceFormat     defaultTexFormat
) :
    mName(passName),
    mResources(resources),
    mDictionary(dictionary),
    mDefaultTexDims(defaultTexDims),
    mDefaultTexFormat(defaultTexFormat)
{
}

const ref<Resource>& RenderData::getResource(std::string_view fieldName) const
{
    std::string qualifiedName;
    qualifiedName.reserve(mName.size() + 1 + fieldName.size());
    qualifiedName.append(mName).append(".").append(fieldName);
    return mResources.getResource(qualifiedName);
}

ref<Texture> RenderData::getTexture(std::string_view name) const
{
    const auto& resource = getResource(name);
    return resource ? resource->asTexture() : nullptr;
}

ref<RenderPass>
RenderPass::create(std::string_view type, ref<Device> device, const Properties& properties, PluginManager& plugins)
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
}  // namespace Falcor
