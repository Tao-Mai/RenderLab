#pragma once

#include "scene/Component.h"
#include "scene/component/TransformComponent.h"
#include "core/Logger.h"

#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class [[=PartialSerialize{}]] Actor
{
public:
    Actor();
    virtual ~Actor()               = default;
    Actor(const Actor&)            = delete;
    Actor& operator=(const Actor&) = delete;

    [[=ReflectField{}]] std::string                             name;
    [[=ReflectField{}]] std::vector<std::unique_ptr<Component>> components;

    virtual void init();
    virtual void tick(float deltaTime);

    template <std::derived_from<Component> T>
    [[nodiscard]] T* getComponent()
    {
        return const_cast<T*>(std::as_const(*this).getComponent<T>());
    }

    template <std::derived_from<Component> T>
    [[nodiscard]] const T* getComponent() const
    {
        for (const auto& component : components)
            if (auto* typed = dynamic_cast<const T*>(component.get()))
                return typed;

        return nullptr;
    }

    template <std::derived_from<Component> T>
    [[nodiscard]] std::vector<T*> getComponents()
    {
        std::vector<T*> result;
        for (const auto& component : components)
            if (auto* typed = dynamic_cast<T*>(component.get()))
                result.push_back(typed);

        return result;
    }

    template <std::derived_from<Component> T>
    [[nodiscard]] std::vector<const T*> getComponents() const
    {
        std::vector<const T*> result;
        for (const auto& component : components)
            if (auto* typed = dynamic_cast<const T*>(component.get()))
                result.push_back(typed);

        return result;
    }

    template <std::derived_from<Component> T, class... Args>
    T& addDefaultComponent(Args&&... args)
    {
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result   = *component;
        components.push_back(std::move(component));

        return result;
    }

    template <std::derived_from<Component> T, class... Args>
    T& addComponent(Args&&... args)
    {
        auto& result = addDefaultComponent<T>(std::forward<Args>(args)...);

        result.init(this);

        return result;
    }

    [[nodiscard]] TransformComponent&       transform();
    [[nodiscard]] const TransformComponent& transform() const;
    [[nodiscard]] uint32_t                  selectionId() const noexcept;

private:
    friend class SceneManager;
    uint32_t id = 0;
};
