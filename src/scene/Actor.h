#pragma once

#include "scene/component/Component.h"
#include "scene/component/TransformComponent.h"

#include <concepts>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class [[=PartialSerialize{}]] Actor
{
public:
    Actor();
    virtual ~Actor() = default;
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    [[=ReflectField{}]] std::string name;
    [[=ReflectField{}]] std::vector<std::unique_ptr<Component>> components;

    virtual void initialize();
    virtual bool tick(float deltaTime);

    template<std::derived_from<Component> T>
    [[nodiscard]] T* getComponent()
    {
        return const_cast<T*>(std::as_const(*this).getComponent<T>());
    }

    template<std::derived_from<Component> T>
    [[nodiscard]] const T* getComponent() const
    {
        for (const auto& component : components)
            if (auto* typed = dynamic_cast<const T*>(component.get())) return typed;

        return nullptr;
    }

    template<std::derived_from<Component> T, class... Args>
    T& addComponent(Args&&... args)
    {
        if (getComponent<T>())
            throw std::logic_error("duplicate component on actor: " + name);

        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        component->owner = this;
        auto& result = *component;
        components.push_back(std::move(component));
        return result;
    }

    [[nodiscard]] TransformComponent& transform();
    [[nodiscard]] const TransformComponent& transform() const;
    [[nodiscard]] uint32_t selectionId() const noexcept;

private:
    friend class SceneManager;
    uint32_t id = 0;
};
