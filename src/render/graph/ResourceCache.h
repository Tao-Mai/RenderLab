#pragma once

#include "core/Macros.h"
#include "core/math/Vector.h"
#include "render/graph/RenderPassReflection.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Falcor
{
class Device;

class FALCOR_API ResourceCache
{
public:
    using ResourcesMap = std::unordered_map<std::string, ref<Resource>>;

    struct DefaultProperties
    {
        uint2          dims{0};
        ResourceFormat format = ResourceFormat::Unknown;
    };

    void registerExternalResource(const std::string& name, const ref<Resource>& resource);
    void registerField(
        const std::string&                 name,
        const RenderPassReflection::Field& field,
        uint32_t                           timePoint,
        const std::string&                 alias = {}
    );
    [[nodiscard]] const ref<Resource>&               getResource(const std::string& name) const;
    [[nodiscard]] const RenderPassReflection::Field& getResourceReflection(const std::string& name) const;
    void allocateResources(ref<Device> device, const DefaultProperties& params);
    void reset();

private:
    struct ResourceData
    {
        RenderPassReflection::Field   field;
        std::pair<uint32_t, uint32_t> lifetime;
        ref<Resource>                 pResource;
        bool                          resolveBindFlags = false;
        std::string                   name;
    };

    std::unordered_map<std::string, uint32_t> mNameToIndex;
    std::vector<ResourceData>                 mResourceData;
    ResourcesMap                              mExternalResources;
};
}  // namespace Falcor
