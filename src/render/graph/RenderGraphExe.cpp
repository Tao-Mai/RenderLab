#include "render/graph/RenderGraphExe.h"

#include "editor/Gui.h"

#include <algorithm>

void RenderGraphExe::execute(RenderContext* context)
{
    for (const auto& entry : passes)
        entry.pass->execute(context, RenderData{entry.ports, resources, defaultTexDims});
}

void RenderGraphExe::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    for (const auto& entry : passes)
    {
        IDScope idScope(entry.pass.get());
        entry.pass->renderUI(context, widget);
    }
}

rhi::ComPtr<rhi::IResource> RenderGraphExe::getResource(const std::string& name) const
{
    return rhi::ComPtr<rhi::IResource>{resources.resource(*resources.findSlot(name))};
}

bool RenderGraphExe::hasPassType(std::string_view type) const
{
    return std::ranges::any_of(passes, [type](const Pass& entry) { return entry.pass->getType() == type; });
}

void RenderGraphExe::setInput(const std::string& name, rhi::IResource* resource)
{
    if (const auto slot = resources.findSlot(name))
        resources.setResource(*slot, resource);
}
