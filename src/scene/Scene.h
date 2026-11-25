#pragma once

#include "asset/Asset.h"
#include "asset/EnvironmentMapAsset.h"
#include "scene/AnalyticLight.h"
#include "scene/Camera.h"
#include "scene/Object.h"

#include <optional>
#include <vector>

struct Scene
{
    using ID                              = AssetID<Scene>;
    static constexpr std::string_view dir = "Scene";

    ID id;
    Camera camera;
    std::vector<Object> objects;
    std::vector<AnalyticLight> analyticLights;
    std::optional<EnvironmentMapAsset::ID> environmentMap;
};
