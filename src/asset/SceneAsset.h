#pragma once

#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <vector>
#include "asset/Asset.h"
#include "asset/EnvironmentMapAsset.h"
#include "editor/EditorCameraConfig.h"
#include "scene/AActor.h"

struct PbrtMediumAsset;

enum class SceneRenderMode
{
    Realtime,
    Offline,
};

struct SceneAsset : Asset
{
    using ID                              = AssetID<SceneAsset>;
    static constexpr std::string_view dir = "Scene";

    ID                                   id;
    SceneRenderMode                      renderMode = SceneRenderMode::Realtime;
    std::optional<AssetID<PbrtMediumAsset>> cameraMedium;
    std::vector<std::unique_ptr<AActor>> actors;
    EditorCameraConfig                   editorCamera;
    std::optional<EnvironmentMapAsset::ID> environmentMap;

    struct Environment
    {
        glm::vec3 up{0.0f, 1.0f, 0.0f};
    } environment;
};
