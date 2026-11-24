#pragma once
#include <slang-rhi.h>

#include "core/Macros.h"
#include "render/graph/RenderPass.h"
#include "render/graph/ResourceCache.h"

#include <vector>

namespace Falcor
{
class FALCOR_API RenderGraphExe
{
public:
    struct Context
    {
        RenderContext* pRenderContext;
        Dictionary&    passesDictionary;
        uint2          defaultTexDims;
    };

    void                                      execute(const Context& context);
    void                                      renderUI(RenderContext* context, Gui::Widgets& widget);
    void                                      renderOverlayUI(RenderContext* context);
    bool                                      onMouseEvent(const MouseEvent& event);
    bool                                      onKeyEvent(const KeyboardEvent& event);
    void                                      onHotReload(HotReloadFlags reloaded);
    [[nodiscard]] rhi::ComPtr<rhi::IResource> getResource(const std::string& name) const;
    void                                      setInput(const std::string& name, rhi::IResource* resource);

private:
    friend class RenderGraphCompiler;
    void insertPass(const std::string& name, const ref<RenderPass>& pass);

    struct Pass
    {
        std::string     name;
        ref<RenderPass> pPass;

    private:
        friend class RenderGraphExe;
        Pass(const std::string& name, const ref<RenderPass>& pass) : name(name), pPass(pass) {}
    };

    std::vector<Pass>              mExecutionList;
    std::unique_ptr<ResourceCache> mpResourceCache;
};
}  // namespace Falcor
