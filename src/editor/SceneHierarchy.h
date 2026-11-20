#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct SceneAsset;
struct SceneObjectActions
{
    std::optional<std::string> createType;
    uint32_t                   deleteId = 0;
    bool                       renamed  = false;
};


class SceneHierarchy
{
public:
    SceneObjectActions draw(SceneAsset& scene, uint32_t& selectedId, uint32_t dockId);
    void             reset()
    {
        renameActorId = 0;
        focusRename   = false;
        renameText.clear();
    }

private:
    uint32_t    renameActorId = 0;
    bool        focusRename   = false;
    std::string renameText;
};
