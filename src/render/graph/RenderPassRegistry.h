#pragma once

#include "core/Logger.h"
#include "render/graph/RenderGraph.h"

#include <map>
#include <memory>
#include <meta>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace graph_value
{
template <class T>
concept Handle = std::is_same_v<T, RenderGraph::ImageHandle> || std::is_same_v<T, RenderGraph::BufferHandle> ||
    std::is_same_v<T, RenderGraph::AccelerationStructureHandle>;

// Python only sees references to these values. All resource handles originate in C++.
template <class T>
nlohmann::json pack(const T& value)
{
    if constexpr (Handle<T>)
    {
        return nlohmann::json::array({std::meta::identifier_of(^^T), value.id});
    }
    else
    {
        static_assert(std::is_aggregate_v<T>, "graph values must be handles or aggregates of handles");
        auto result = nlohmann::json::object();
        constexpr auto kAccess = std::meta::access_context::current();
        template for (constexpr auto kMember :
            std::define_static_array(std::meta::nonstatic_data_members_of(^^T, kAccess)))
        {
            result[std::meta::identifier_of(kMember)] = pack(value.[:kMember:]);
        }
        return result;
    }
}

template <class T>
T unpack(const nlohmann::json& value)
{
    if constexpr (Handle<T>)
    {
        constexpr auto kKind = std::meta::identifier_of(^^T);
        CHECK(value.is_array() && value.size() == 2 && value[0] == kKind, "graph connection requires {}", kKind);
        return T{value[1].get<RenderGraph::ResourceHandle>()};
    }
    else
    {
        static_assert(std::is_aggregate_v<T>, "graph values must be handles or aggregates of handles");
        CHECK(value.is_object(), "graph connection requires {} outputs", std::meta::identifier_of(^^T));
        T result{};
        constexpr auto kAccess = std::meta::access_context::current();
        template for (constexpr auto kMember :
            std::define_static_array(std::meta::nonstatic_data_members_of(^^T, kAccess)))
        {
            constexpr auto kName = std::meta::identifier_of(kMember);
            const auto field = value.find(kName);
            CHECK(field != value.end(), "graph connection is missing output '{}'", kName);
            result.[:kMember:] = unpack<std::remove_cvref_t<decltype(result.[:kMember:])>>(*field);
        }
        return result;
    }
}
}

class RenderPassRegistry
{
public:
    RenderPassRegistry() = default;
    RenderPassRegistry(RenderPassRegistry&&) noexcept = default;
    RenderPassRegistry& operator=(RenderPassRegistry&&) noexcept = default;

    template <class Pass>
    void add(std::string_view name = std::meta::identifier_of(^^Pass))
    {
        const bool added = factories.try_emplace(std::string{name}, describe(&Pass::setup)).second;
        CHECK(added, "render pass type '{}' is already registered", name);
    }

    [[nodiscard]] nlohmann::json build(
        RenderGraph& graph, const PassSetup& setup, const nlohmann::json& description, nlohmann::json inputs
    );
    void clearInstances() noexcept;

    template <class Pass>
    [[nodiscard]] const Pass* activePass() const
    {
        const auto found = active.find(typeid(Pass));
        return found == active.end() ? nullptr : static_cast<const Pass*>(found->second);
    }

private:
    using Arguments = std::span<const nlohmann::json* const>;

    struct Factory
    {
        std::type_index type;
        std::shared_ptr<void> (*create)();
        nlohmann::json (*setup)(void*, RenderGraph&, const PassSetup&, Arguments, const nlohmann::json&);
    };

    template <class Pass, class Output, class... Inputs>
    static Factory describe(Output (Pass::*)(RenderGraph&, const PassSetup&, Inputs...))
    {
        return {
            typeid(Pass),
            []() -> std::shared_ptr<void> { return std::make_shared<Pass>(); },
            [](void* instance, RenderGraph& graph, const PassSetup& setup, Arguments arguments,
               const nlohmann::json& properties)
            {
                CHECK(
                    arguments.size() == sizeof...(Inputs),
                    "pass '{}' requires {} inputs, got {}",
                    std::meta::identifier_of(^^Pass),
                    sizeof...(Inputs),
                    arguments.size()
                );
                auto& pass = *static_cast<Pass*>(instance);
                if constexpr (requires { pass.configure(properties); })
                    pass.configure(properties);
                else
                    CHECK(properties.empty(), "pass '{}' has no script properties", std::meta::identifier_of(^^Pass));

                return [&]<size_t... Index>(std::index_sequence<Index...>)
                {
                    if constexpr (requires(vk::raii::CommandBuffer& commands, const PassFrame& frame)
                                  { pass.execute(graph, commands, frame); })
                    {
                        return graph_value::pack(graph.addPass(
                            pass, setup, graph_value::unpack<std::remove_cvref_t<Inputs>>(*arguments[Index])...
                        ));
                    }
                    else
                    {
                        return graph_value::pack(
                            pass.setup(graph, setup, graph_value::unpack<std::remove_cvref_t<Inputs>>(*arguments[Index])...)
                        );
                    }
                }(std::index_sequence_for<Inputs...>{});
            },
        };
    }

    std::map<std::string, Factory, std::less<>> factories;
    // Preserve pass-owned pipelines and asynchronous work across graph rebuilds.
    std::map<std::pair<std::string, std::string>, std::shared_ptr<void>> instances;
    std::map<std::type_index, const void*> active;
};
