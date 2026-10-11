#pragma once
#include <cstdint>
#include <optional>
#include "asset/AssetID.h"
struct SceneAsset;
struct SceneActions
{
    std::optional<AssetID<SceneAsset>> sceneId;
    std::optional<std::string>         createType;
    uint32_t                           deleteId = 0;
    bool                               renamed  = false;
};


class SceneHierarchy
{
public:
    SceneActions draw(SceneAsset& scene, uint32_t& selectedId, uint32_t dockId);
    void         reset()
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
