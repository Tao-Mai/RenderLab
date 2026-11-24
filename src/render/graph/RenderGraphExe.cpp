#include "render/graph/RenderGraphExe.h"

#include "editor/Gui.h"
#include "render/RenderContext.h"

#include <algorithm>

void RenderGraphExe::insertPass(const std::string& name, const ref<RenderPass>& pass)
{
    mExecutionList.push_back(Pass{name, pass});
}

void RenderGraphExe::execute(const Context& context)
{
    auto* renderContext = context.pRenderContext;
    for (const auto& entry : mExecutionList)
    {
        const RenderData renderData{
            entry.name,
            *mpResourceCache,
            context.passesDictionary,
            context.defaultTexDims,
        };
        entry.pPass->execute(renderContext, renderData);
    }
}

void RenderGraphExe::renderUI(RenderContext* context, Gui::Widgets& widget)
{
    for (const auto& pass : mExecutionList)
    {
        IDScope idScope(pass.pPass.get());
        pass.pPass->renderUI(context, widget);
    }
}

rhi::ComPtr<rhi::IResource> RenderGraphExe::getResource(const std::string& name) const
{
    return mpResourceCache->getResource(name);
}

bool RenderGraphExe::hasPassType(std::string_view type) const
{
    return std::ranges::any_of(mExecutionList, [type](const Pass& pass) { return pass.pPass->getType() == type; });
}

void RenderGraphExe::setInput(const std::string& name, rhi::IResource* resource)
{
    mpResourceCache->registerExternalResource(name, resource);
}
