#pragma once

#include <glm/vec3.hpp>
#include <memory>
#include <vector>
#include "asset/Asset.h"
#include "editor/EditorCameraConfig.h"
#include "scene/AActor.h"

struct SceneAsset : Asset
{
    using ID                              = AssetID<SceneAsset>;
    static constexpr std::string_view dir = "Scene";

    ID                                   id;
    std::vector<std::unique_ptr<AActor>> actors;
    EditorCameraConfig                   editorCamera;

    struct Environment
    {
        glm::vec3 up{0.0f, 1.0f, 0.0f};
    } environment;
};
