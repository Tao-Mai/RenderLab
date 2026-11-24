#pragma once
#include <slang-rhi.h>

#include "core/Serializer.h"
#include "editor/Gui.h"

#include <functional>
#include <optional>

class RenderPass;
struct EditorCameraConfig;

struct PassEditorContext
{
    rhi::IDevice&                                                                 device;
    const EditorCameraConfig&                                                     camera;
    std::function<void(const RenderPass&, std::optional<nlohmann::json>)> submit;
};

namespace pass_editor
{
void draw(const RenderPass& pass, const PassEditorContext& context, Gui::Widgets& widgets);
bool drawFields(Gui::Widgets& widgets, void* settings, const metadata::Record& record);

template <class Pass, auto Member, auto Draw = nullptr>
void edit(const RenderPass& pass, const PassEditorContext& context, Gui::Widgets& widgets)
{
    const auto& typedPass = static_cast<const Pass&>(pass);
    auto settings = typedPass.*Member;
    bool changed;
    if constexpr (Draw != nullptr)
        changed = Draw(widgets, settings, typedPass, context);
    else
        changed = drawFields(widgets, &settings, metadata::record(typeid(settings)));

    if (changed)
        context.submit(pass, Serialize(settings));
}
}  // namespace pass_editor
