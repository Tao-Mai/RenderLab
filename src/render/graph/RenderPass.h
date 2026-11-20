#pragma once

#include "core/Debug.h"
#include "render/graph/RenderGraph.h"

#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

class PluginManager;

enum class PortKind
{
    Image,
    Buffer,
};

using GraphResource = std::variant<RenderGraph::ImageHandle, RenderGraph::BufferHandle>;
using GraphResources = std::map<std::string, GraphResource, std::less<>>;

struct RenderPassPort
{
    std::string name;
    PortKind    kind;
    bool        optional;
    vk::Format  format;
};

class RenderPassReflection
{
public:
    RenderPassReflection& input(std::string_view name, PortKind kind, bool optional = false,
                                vk::Format format = vk::Format::eUndefined);
    RenderPassReflection& output(std::string_view name, PortKind kind, bool optional = false,
                                 vk::Format format = vk::Format::eUndefined);
    [[nodiscard]] std::span<const RenderPassPort> inputs() const { return inputPorts; }
    [[nodiscard]] std::span<const RenderPassPort> outputs() const { return outputPorts; }

private:
    std::vector<RenderPassPort> inputPorts;
    std::vector<RenderPassPort> outputPorts;
};

class PassResources
{
public:
    explicit PassResources(GraphResources inputs) : inputResources(std::move(inputs)) {}
    [[nodiscard]] RenderGraph::ImageHandle image(std::string_view name) const;
    [[nodiscard]] RenderGraph::BufferHandle buffer(std::string_view name) const;
    void setOutput(std::string_view name, GraphResource resource);
    [[nodiscard]] const GraphResources& outputs() const { return outputResources; }

private:
    GraphResources inputResources;
    GraphResources outputResources;
};

enum class PassExecution
{
    Commands,
    Composite,
};

class RenderPass
{
public:
    explicit RenderPass(PassExecution execution = PassExecution::Commands) : executionMode(execution) {}
    virtual ~RenderPass();

    [[nodiscard]] static std::shared_ptr<RenderPass> create(
        const PluginManager& plugins, std::string_view type
    );
    [[nodiscard]] PassExecution execution() const { return executionMode; }

    [[nodiscard]] virtual RenderPassReflection reflect(const PassSetup& setup) const = 0;
    virtual void compile(RenderGraph& graph, const PassSetup& setup, PassResources& resources) = 0;
    virtual void bindResources(RenderGraph&, const PassSetup&) {}
    virtual void prepare(RenderGraph&, const PassFrame&) {}
    virtual void execute(RenderGraph&, vk::raii::CommandBuffer&, const PassFrame&) { DCHECK(false); }

    [[nodiscard]] virtual nlohmann::json properties() const;
    virtual bool setProperties(const nlohmann::json& properties);
    [[nodiscard]] virtual nlohmann::json statistics() const;

private:
    PassExecution executionMode;
};
