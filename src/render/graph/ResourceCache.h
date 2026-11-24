#pragma once

#include <slang-rhi.h>
#include "render/graph/RenderPassReflection.h"

#include <glm/vec2.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class ResourceCache
{
public:
    using ResourceDesc = RenderPassReflection::ResourceDesc;
    using ResourcesMap = std::unordered_map<std::string, rhi::ComPtr<rhi::IResource>>;
    using Ports        = std::vector<std::pair<std::string, size_t>>;

    struct DefaultProperties
    {
        glm::uvec2  dims{0};
        rhi::Format format = rhi::Format::Undefined;

        bool operator==(const DefaultProperties&) const = default;
    };

    [[nodiscard]] static std::optional<ResourceDesc> describe(rhi::IResource& resource);
    [[nodiscard]] static bool                        sameDescription(const ResourceDesc& a, const ResourceDesc& b);

    void addResource(std::string name, const ResourceDesc& desc);
    void addInput(std::string name, const ResourceDesc& requirements, rhi::IResource* resource, bool validate);
    void connect(std::string name, const std::string& sourceName, const ResourceDesc& desc, bool validate);
    void markOutput(const std::string& name);
    void allocateResources(rhi::IDevice& device, const DefaultProperties& defaults);

    [[nodiscard]] std::optional<size_t> findSlot(const std::string& name) const;
    [[nodiscard]] ResourceDesc          description(size_t slot, const DefaultProperties& defaults) const;
    [[nodiscard]] rhi::IResource*       resource(size_t slot) const { return slots[slot].resource; }
    void                   setInput(size_t slot, rhi::IResource* resource) { slots[slot].resource = resource; }
    [[nodiscard]] uint64_t historyRevision(size_t slot) const { return slots[slot].historyRevision; }
    void                   invalidateHistory(size_t slot) { ++slots[slot].historyRevision; }

private:
    struct Slot
    {
        ResourceDesc                resourceDesc;
        rhi::ComPtr<rhi::IResource> resource;
        std::string                 name;
        bool                        external        = false;
        uint64_t                    historyRevision = 0;
    };

    std::unordered_map<std::string, size_t> indices;
    std::vector<Slot>                       slots;
};
