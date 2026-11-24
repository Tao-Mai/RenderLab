#pragma once
#include <slang-rhi.h>

#include "core/Macros.h"
#include "render/graph/RenderPass.h"
#include "render/graph/ResourceCache.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Falcor
{
struct RenderContext;

class RenderGraph;
class RenderGraphExe;

class FALCOR_API RenderGraphCompiler
{
public:
    struct Dependencies
    {
        ResourceCache::DefaultProperties defaultResourceProps;
        ResourceCache::ResourcesMap      externalResources;
    };

    [[nodiscard]] static std::unique_ptr<RenderGraphExe>
    compile(RenderGraph& graph, RenderContext* context, const Dependencies& dependencies);

private:
    RenderGraphCompiler(RenderGraph& graph, const Dependencies& dependencies);

    struct PassData
    {
        uint32_t             index;
        ref<RenderPass>      pPass;
        std::string          name;
        RenderPassReflection reflector;
    };

    RenderGraph&              mGraph;
    rhi::ComPtr<rhi::IDevice> mpDevice;
    const Dependencies&       mDependencies;
    std::vector<PassData>     mExecutionList;
    struct
    {
        std::vector<std::string>                         generatedPasses;
        std::vector<std::pair<std::string, std::string>> removedEdges;
    } mCompilationChanges;

    void                    resolveExecutionOrder();
    void                    compilePasses(RenderContext* context);
    bool                    insertAutoPasses();
    void                    allocateResources(rhi::IDevice* device, ResourceCache* cache);
    void                    validateGraph() const;
    void                    restoreCompilationChanges();
    RenderPass::CompileData prepPassCompilationData(const PassData& passData);
};
}  // namespace Falcor
