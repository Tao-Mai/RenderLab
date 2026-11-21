#include "render/graph/RenderGraphExe.h"

#include "core/profiling/Profiler.h"
#include "editor/Gui.h"
#include "render/RenderContext.h"

#include <algorithm>

namespace Falcor
{
void RenderGraphExe::insertPass(const std::string& name, const ref<RenderPass>& pass)
{
    mExecutionList.push_back(Pass{name, pass});
}

void RenderGraphExe::execute(const Context& context)
{
    auto* renderContext = context.pRenderContext;
    for (const auto& entry : mExecutionList)
    {
        FALCOR_PROFILE(renderContext, entry.name);
        const RenderData renderData{
            entry.name,
            *mpResourceCache,
            context.passesDictionary,
            context.defaultTexDims,
            context.defaultTexFormat,
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

void RenderGraphExe::renderOverlayUI(RenderContext* context)
{
    for (const auto& pass : mExecutionList)
        pass.pPass->renderOverlayUI(context);
}

bool RenderGraphExe::onMouseEvent(const MouseEvent& event)
{
    return std::ranges::any_of(mExecutionList, [&](const auto& pass) { return pass.pPass->onMouseEvent(event); });
}

bool RenderGraphExe::onKeyEvent(const KeyboardEvent& event)
{
    return std::ranges::any_of(mExecutionList, [&](const auto& pass) { return pass.pPass->onKeyEvent(event); });
}

void RenderGraphExe::onHotReload(HotReloadFlags reloaded)
{
    for (const auto& pass : mExecutionList)
        pass.pPass->onHotReload(reloaded);
}

ref<Resource> RenderGraphExe::getResource(const std::string& name) const
{
    return mpResourceCache->getResource(name);
}

void RenderGraphExe::setInput(const std::string& name, const ref<Resource>& resource)
{
    mpResourceCache->registerExternalResource(name, resource);
}
}  // namespace Falcor
