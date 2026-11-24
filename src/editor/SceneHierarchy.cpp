#include "editor/SceneHierarchy.h"
#include <algorithm>
#include <imgui.h>
#include <imgui_stdlib.h>
#include "asset/SceneAsset.h"
#include "core/Logger.h"
#include "core/Metadata.h"
#include "core/TypeRegistry.h"
namespace
{
using ActorType = TypeRegistry<AActor>::Entry;

void drawActorTypeTree(std::string_view parent, const std::vector<const ActorType*>& types,
                       std::optional<std::string>& chosen)
{
    for (const auto* type : types)
    {
        if (type->parent != parent)
            continue;

        const bool hasChildren = std::ranges::any_of(types, [&](const auto* other) { return other->parent == type->id; });
        ImGuiTreeNodeFlags flags =
            ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        const bool open = ImGui::TreeNodeEx(type->id, flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            chosen = type->id;
        if (open && hasChildren)
        {
            drawActorTypeTree(type->id, types, chosen);
            ImGui::TreePop();
        }
    }
}
}  // namespace
SceneObjectActions SceneHierarchy::draw(SceneAsset& scene, uint32_t& selectedId, uint32_t hierarchyDockId)
{
    SceneObjectActions result;
    ImGui::SetNextWindowDockID(hierarchyDockId, ImGuiCond_Always);
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Scene Objects", nullptr, flags);

    if (ImGui::Button("+"))
        ImGui::OpenPopup("New Actor");
    ImGui::SetItemTooltip("Add an object");
    if (ImGui::BeginPopup("New Actor"))
    {
        ImGui::TextUnformatted("Choose an Actor class");
        ImGui::Separator();
        std::vector<const ActorType*> types;
        for (const auto& [id, entry] : TypeRegistry<AActor>::get().entries)
            types.push_back(&entry);
        std::ranges::sort(types, {}, [](const auto* type) { return std::string_view{type->id}; });

        const auto* actorId = metadata::record(typeid(AActor)).id;
        if (ImGui::TreeNodeEx(actorId, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            drawActorTypeTree(actorId, types, result.createType);
            ImGui::TreePop();
        }
        if (result.createType)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();
    if (renameActorId &&
        !std::ranges::any_of(scene.actors, [&](const auto& actor) { return actor->selectionId() == renameActorId; }))
    {
        renameActorId = 0;
        focusRename   = false;
        renameText.clear();
    }

    if (ImGui::BeginChild("Actor list", {0, 0}))
    {
        for (const auto& actor : scene.actors)
        {
            ImGui::PushID(static_cast<int>(actor->selectionId()));
            if (actor->selectionId() == renameActorId)
            {
                if (focusRename)
                {
                    ImGui::SetKeyboardFocusHere();
                    focusRename = false;
                }
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ImGui::GetStyle().FramePadding.x, 0.0f});
                const bool enter = ImGui::InputText(
                    "##Actor name",
                    &renameText,
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll
                );
                const bool finished = enter || ImGui::IsItemDeactivated();
                ImGui::PopStyleVar();

                if (finished)
                {
                    if (!renameText.empty() && actor->name != renameText)
                    {
                        actor->name    = renameText;
                        result.renamed = true;
                    }
                    renameActorId = 0;
                    renameText.clear();
                }
            }
            else
            {
                const std::string label = actor->name + "###Actor";
                if (ImGui::Selectable(label.c_str(), actor->selectionId() == selectedId))
                    selectedId = actor->selectionId();
            }

            if (ImGui::BeginPopupContextItem("Actor actions"))
            {
                selectedId = actor->selectionId();
                if (ImGui::MenuItem("Rename"))
                {
                    renameActorId = selectedId;
                    renameText    = actor->name;
                    focusRename   = true;
                }
                if (ImGui::MenuItem("Delete"))
                    result.deleteId = actor->selectionId();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return result;
}
