#pragma once

#include "editor/Gui.h"

#include <slang-rhi.h>

class RenderPass;
struct Camera;
struct RenderPassOptions;

struct PassEditorContext
{
    rhi::IDevice& device;

    const Camera& camera;
};

namespace pass_editor
{
void draw(const RenderPass& pass, const PassEditorContext& context, Gui::Widgets& widgets, RenderPassOptions& options);
}
