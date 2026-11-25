#pragma once

#include "editor/Gui.h"
#include "editor/PassEditor.h"
#include "render/graph/RenderPass.h"
#include "render/graph/RenderPassOptions.h"

#include <type_traits>
#include <utility>

namespace pass_settings
{
template <auto Member, auto Option, auto Draw, auto OnUpdate = nullptr>
constexpr RenderPass::SettingsBinding bind()
{
    return []<class Pass, class Settings>(Settings Pass::*) -> RenderPass::SettingsBinding
    {
        return {
            .apply = [](RenderPass& pass, const RenderPassOptions& options)
            {
                auto& typed = static_cast<Pass&>(pass);
                auto& current = typed.*Member;
                const auto& next = options.*Option;
                if (current == next)
                    return;

                next.validate();
                if constexpr (std::is_member_function_pointer_v<decltype(OnUpdate)>)
                {
                    const auto previous = std::exchange(current, next);
                    (typed.*OnUpdate)(previous);
                }
                else
                {
                    current = next;
                    if constexpr (OnUpdate != nullptr)
                        typed.*OnUpdate = true;
                }
            },
            .drawEditor = [](
                const RenderPass& pass, const PassEditorContext& context, Gui::Widgets& widgets,
                RenderPassOptions& options, bool reset
            )
            {
                auto& current = options.*Option;
                if (reset)
                {
                    current = {};

                    return;
                }

                auto edited = current;
                if (Draw(widgets, edited, static_cast<const Pass&>(pass), context))
                    current = std::move(edited);
            },
        };
    }(Member);
}
}  // namespace pass_settings
