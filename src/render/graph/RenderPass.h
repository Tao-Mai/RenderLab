#pragma once
#include <slang-rhi.h>

#include "core/Macros.h"
#include "core/Object.h"
#include "core/Plugin.h"
#include "core/Properties.h"
#include <glm/vec2.hpp>
#include "editor/Gui.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassReflection.h"
#include "render/graph/ResourceCache.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

class RenderGraphCompiler;
class RenderGraphExe;

class RENDERLAB_CORE_API RenderData
{
public:
    // Resources are owned by the graph.
    [[nodiscard]] rhi::IResource* getResource(std::string_view name) const;
    [[nodiscard]] rhi::ITexture*  getTexture(std::string_view name) const;
    [[nodiscard]] rhi::IBuffer*   getBuffer(std::string_view name) const;
    void                          setBuffer(std::string_view name, rhi::IBuffer* buffer) const;

    [[nodiscard]] uint64_t          getHistoryRevision(std::string_view name) const;
    void                            invalidateHistory(std::string_view name) const;
    [[nodiscard]] const glm::uvec2& getDefaultTextureDims() const { return mDefaultTexDims; }

private:
    friend class RenderGraphExe;
    RenderData(const ResourceCache::Ports& ports, ResourceCache& resources, const glm::uvec2& defaultTexDims) :
        ports(ports), resources(resources), mDefaultTexDims(defaultTexDims)
    {
    }

    const ResourceCache::Ports& ports;
    ResourceCache& resources;
    glm::uvec2 mDefaultTexDims;

    [[nodiscard]] std::optional<size_t> findSlot(std::string_view name) const;
};

class RENDERLAB_CORE_API RenderPass : public Object
{
    RL_OBJECT(RenderPass)

public:
    using PluginCreate = std::function<ref<RenderPass>(rhi::ComPtr<rhi::IDevice>, const Properties&)>;

    struct PluginInfo
    {
        std::string desc;
    };

    RL_PLUGIN_BASE_CLASS(RenderPass);

    struct CompileData
    {
        glm::uvec2                defaultTexDims{0};
        rhi::Format       defaultTexFormat = rhi::Format::Undefined;
        RenderPassReflection connectedResources;
    };

    [[nodiscard]] static ref<RenderPass> create(
        std::string_view          type,
        rhi::ComPtr<rhi::IDevice> device,
        const Properties&         properties = {},
        PluginManager&            plugins    = PluginManager::instance()
    );
    ~RenderPass() override = default;

    [[nodiscard]] const std::string& getType() const { return getPluginType(); }
    [[nodiscard]] const std::string& getDesc() const { return getPluginInfo().desc; }
    [[nodiscard]] uint32_t           getMaxFramesInFlight() const { return mMaxFramesInFlight; }

    [[nodiscard]] virtual RenderPassReflection reflect(const CompileData& compileData) = 0;
    virtual void                               compile(RenderContext* context, const CompileData& compileData) {}
    virtual void                               execute(RenderContext* context, const RenderData& renderData) = 0;

    virtual void                     setProperties(const Properties& properties);
    [[nodiscard]] virtual Properties getProperties() const { return {}; }
    virtual void                     renderUI(const PassEditorContext& context, Gui::Widgets& widget) {}

    [[nodiscard]] const std::string& getName() const { return mName; }

protected:
    explicit RenderPass(rhi::ComPtr<rhi::IDevice> device, uint32_t maxFramesInFlight = kMaxFramesInFlight) :
        mpDevice(std::move(device)), mMaxFramesInFlight(maxFramesInFlight)
    {
    }
    void requestRecompile() { mPassChangedCB(); }

    rhi::ComPtr<rhi::IDevice> mpDevice;
    std::string               mName;
    std::function<void()>     mPassChangedCB = [] {};

    friend class RenderGraph;

private:
    uint32_t    mMaxFramesInFlight;
};
