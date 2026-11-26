#pragma once

#include "scene/Scene.h"

#include <string>
#include <string_view>
#include <vector>

struct SceneLayer
{
    using ID = AssetID<SceneLayer>;
    static constexpr std::string_view dir = "SceneLayer";

    ID id;

    Scene::ID baseScene;

    std::string name;

    std::vector<Object> objects;
    std::vector<AnalyticLight> analyticLights;
};
