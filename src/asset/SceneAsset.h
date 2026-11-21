#pragma once

#include "core/MetadataAnnotations.h"

#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <vector>
#include "asset/Asset.h"
#include "asset/EnvironmentMapAsset.h"
#include "editor/EditorCameraConfig.h"
#include "scene/AActor.h"

struct PbrtMediumAsset;

enum class RL_ENUM SceneRenderMode
{
    Realtime RL_ENUM_VALUE("Realtime"),
    Offline RL_ENUM_VALUE("Offline"),
};

struct RL_RECORD SceneAsset : Asset
{
    using ID                              = AssetID<SceneAsset>;
    static constexpr std::string_view dir = "Scene";

    RL_FIELD("id") ID                                   id;
    RL_FIELD("renderMode") SceneRenderMode                      renderMode = SceneRenderMode::Realtime;
    RL_FIELD("cameraMedium") std::optional<AssetID<PbrtMediumAsset>> cameraMedium;
    RL_FIELD("actors") std::vector<std::unique_ptr<AActor>> actors;
    RL_FIELD("editorCamera") EditorCameraConfig                   editorCamera;
    RL_FIELD("environmentMap") std::optional<EnvironmentMapAsset::ID> environmentMap;

    struct RL_RECORD Environment
    {
        RL_FIELD("up") glm::vec3 up{0.0f, 1.0f, 0.0f};
    } environment RL_FIELD("environment");
};

#include RL_METADATA_HEADER("metadata/src/asset/SceneAsset.generated.h")
