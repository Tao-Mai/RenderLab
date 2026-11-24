#include "render/graph/RenderGraphExe.h"

#include "editor/PassEditor.h"

#include <algorithm>

void RenderGraphExe::execute(RenderContext* context, RenderData::Dictionary& dictionary, glm::uvec2 defaultTexDims)
{
    for (const auto& entry : passes)
        entry.pass->execute(context, RenderData{entry.ports, resources, dictionary, defaultTexDims});
}

void RenderGraphExe::renderUI(const PassEditorContext& context, Gui::Widgets& widget)
{
    for (const auto& entry : passes)
        pass_editor::draw(*entry.pass, context, widget);
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
        resources.setInput(*slot, resource);
}
