#pragma once

#include "render/graph/RenderPass.h"
#include "render/graph/ResourceCache.h"

#include <memory>
#include <unordered_set>
#include <vector>

class RenderGraph;
class RenderGraphExe;

class RenderGraphCompiler
{
public:
    struct Dependencies
    {
        ResourceCache::DefaultProperties defaultResourceProps;
        ResourceCache::ResourcesMap      externalResources;
    };

    [[nodiscard]] static std::unique_ptr<RenderGraphExe>
    compile(const RenderGraph& graph, RenderContext* context, const Dependencies& dependencies);

private:
    struct PassData
    {
        uint32_t                             index;
        std::string                          name;
        std::shared_ptr<RenderPass>          pass;
        RenderPassReflection                 reflection;
        std::unordered_set<std::string_view> usedOutputs;
    };

    RenderGraphCompiler(const RenderGraph& graph, const Dependencies& dependencies) :
        graph(graph), dependencies(dependencies)
    {
    }

    void preparePasses();
    void resolveResources(ResourceCache& resources);
    void validateGraph(const ResourceCache& resources) const;
    void registerResources(ResourceCache& resources) const;

    const RenderGraph&    graph;
    const Dependencies&   dependencies;
    std::vector<PassData> passes;
};
