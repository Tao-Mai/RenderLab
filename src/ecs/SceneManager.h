#pragma once

#include "asset/Asset.h"

#include <span>
#include <string_view>
#include <vector>

#include <entt/entity/registry.hpp>

class SceneManager
{
public:
    static constexpr std::string_view editorCameraName = "Editor Camera";

    void load(const Scene::ID& id);
    void load(const Scene::Desc& scene);
    void save();
    void reset() noexcept;

    [[nodiscard]] Scene::Desc& scene() noexcept;
    [[nodiscard]] const Scene::Desc& scene() const noexcept;
    [[nodiscard]] entt::registry& registry() noexcept;
    [[nodiscard]] const entt::registry& registry() const noexcept;
    [[nodiscard]] std::span<const entt::entity> entities() const noexcept;
    [[nodiscard]] entt::entity editorCamera() const noexcept;
    [[nodiscard]] Scene::Desc::Object* findObject(uint32_t selectionId);

    // Zero is reserved for the picking background; both mesh and light use the entity's ID.
    [[nodiscard]] static uint32_t selectionId(entt::entity entity);

private:
    entt::registry entityRegistry;
    Scene::Desc data;
    std::vector<entt::entity> objectEntities;
    entt::entity cameraEntity = entt::null;

    [[nodiscard]] static Scene::Desc copyScene(const Scene::Desc& scene);
};
