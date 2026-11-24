#pragma once
#include <slang-rhi.h>

#include "core/Serializer.h"
#include "editor/Gui.h"

#include <functional>
#include <optional>
#include <string_view>

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
bool begin(const RenderPass& pass, const PassEditorContext& context, std::string_view label);
bool drawFields(Gui::Widgets& widgets, void* settings, const metadata::Record& record);
}  // namespace pass_editor

template <auto Draw = nullptr, class Pass, class Settings>
void drawPassEditor(
    Gui::Widgets&    widgets,
    const Pass&              pass,
    const Settings&          parameters,
    const PassEditorContext& context,
    std::string_view         label
)
{
    if (!pass_editor::begin(pass, context, label))
        return;

    auto settings = parameters;
    bool changed;
    if constexpr (Draw != nullptr)
        changed = Draw(widgets, settings, pass, context);
    else
        changed = pass_editor::drawFields(widgets, &settings, metadata::record(typeid(Settings)));

    if (changed)
        context.submit(pass, Serialize(settings));
}
