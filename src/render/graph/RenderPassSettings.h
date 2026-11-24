#pragma once

#include "core/Serializer.h"
#include "editor/PassEditor.h"
#include "render/graph/RenderPass.h"

#include <type_traits>
#include <utility>

namespace pass_settings
{
enum class Update
{
    Replace,
    Patch,
};

// OnUpdate names a change flag or a callback that receives the previous settings.
template <auto Member, auto Draw = nullptr, auto OnUpdate = nullptr, Update Mode = Update::Replace>
constexpr RenderPass::SettingsBinding bind()
{
    return []<class Pass, class Settings>(Settings Pass::*) -> RenderPass::SettingsBinding
    {
        return {
            .get = [](const RenderPass& pass) { return Serialize(static_cast<const Pass&>(pass).*Member); },
            .set = [](RenderPass& pass, const nlohmann::json& properties)
            {
                auto& typedPass = static_cast<Pass&>(pass);
                auto& current = typedPass.*Member;

                Settings next;
                if constexpr (Mode == Update::Patch)
                    next = DeserializeWithDefaults(properties, current);
                else
                    Deserialize(properties, next);
                next.validate();

                if constexpr (std::is_member_function_pointer_v<decltype(OnUpdate)>)
                {
                    const auto previous = std::exchange(current, std::move(next));
                    (typedPass.*OnUpdate)(previous);
                }
                else
                {
                    if constexpr (OnUpdate != nullptr)
                    {
                        bool& changed = typedPass.*OnUpdate;
                        if (!changed)
                            metadata::visitChangedFields(
                                current, next, [&changed](const metadata::Property&) { changed = true; }
                            );
                    }
                    current = std::move(next);
                }
            },
            .drawEditor = pass_editor::edit<Pass, Member, Draw>,
        };
    }(Member);
}
}  // namespace pass_settings
