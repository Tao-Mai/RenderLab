#pragma once

#include "asset/SceneAsset.h"
#include "scene/IScene.h"

#include <utility>

namespace Falcor
{
class Scene final : public IScene
{
    FALCOR_OBJECT(Scene)

public:
    explicit Scene(SceneAsset asset = {}) : asset(std::move(asset)) {}

    [[nodiscard]] SceneAsset&       getAsset() { return asset; }
    [[nodiscard]] const SceneAsset& getAsset() const { return asset; }

private:
    SceneAsset asset;
};
}  // namespace Falcor
