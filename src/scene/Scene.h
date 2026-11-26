#pragma once

#include "asset/Asset.h"
#include "asset/EnvironmentMapAsset.h"
#include "scene/AnalyticLight.h"
#include "scene/Camera.h"
#include "scene/Object.h"

#include <optional>
#include <vector>

struct SceneLayer;

struct Scene
{
    enum class Kind
    {
        Environment,
        ModelPreview,
    };

    using ID                              = AssetID<Scene>;
    static constexpr std::string_view dir = "Scene";

    ID id;
    Kind kind = Kind::Environment;
    Camera camera;
    std::optional<EnvironmentMapAsset::ID> environmentMap;

    std::vector<Object> objects;
    std::vector<AnalyticLight> analyticLights;

    std::optional<AssetID<SceneLayer>> defaultLayer;
};
