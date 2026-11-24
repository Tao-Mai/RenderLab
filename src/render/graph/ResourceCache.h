#pragma once
#include <slang-rhi.h>

#include "core/Macros.h"
#include "core/math/Vector.h"
#include "render/graph/RenderPassReflection.h"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Falcor
{

class FALCOR_API ResourceCache
{
public:
    using ResourcesMap = std::unordered_map<std::string, rhi::ComPtr<rhi::IResource>>;

    struct DefaultProperties
    {
        uint2          dims{0};
        rhi::Format format = rhi::Format::Undefined;
    };

    void registerExternalResource(const std::string& name, rhi::IResource* resource);
    void registerField(
        const std::string& name, const RenderPassReflection::Field& field, const std::string& alias = {}
    );
    [[nodiscard]] const rhi::ComPtr<rhi::IResource>& getResource(const std::string& name) const;
    void                                             setResource(const std::string& name, rhi::IResource* resource);
    void allocateResources(rhi::IDevice* device, const DefaultProperties& params);

private:
    struct ResourceData
    {
        RenderPassReflection::Field   field;
        rhi::ComPtr<rhi::IResource>   pResource;
        bool                          resolveBindFlags = false;
        std::string                   name;
    };

    std::unordered_map<std::string, uint32_t> mNameToIndex;
    std::vector<ResourceData>                 mResourceData;
    ResourcesMap                              mExternalResources;
};
}  // namespace Falcor
