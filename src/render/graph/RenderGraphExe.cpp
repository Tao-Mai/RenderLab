#include "render/graph/RenderGraphExe.h"

#include "editor/PassEditor.h"

void RenderGraphExe::execute(RenderContext* context, RenderData::Dictionary& dictionary, glm::uvec2 defaultTexDims)
{
    for (const auto& entry : passes)
        entry.pass->execute(context, RenderData{entry.ports, resources, dictionary, defaultTexDims});
}

void RenderGraphExe::renderUI(const PassEditorContext& context, Gui::Widgets& widgets, RenderPassOptions& options)
{
    for (const auto& entry : passes)
        pass_editor::draw(*entry.pass, context, widgets, options);
}

rhi::ComPtr<rhi::IResource> RenderGraphExe::getResource(const std::string& name) const
{
    return rhi::ComPtr<rhi::IResource>{resources.resource(*resources.findSlot(name))};
}

void RenderGraphExe::setInput(const std::string& name, rhi::IResource* resource)
{
    if (const auto slot = resources.findSlot(name))
        resources.setInput(*slot, resource);
}
