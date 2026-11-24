#pragma once
#include <slang-rhi.h>

#include "core/Macros.h"
#include <glm/vec2.hpp>
#include "editor/Gui.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassReflection.h"
#include "render/graph/ResourceCache.h"
#include "render/plugins/Plugin.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

class RenderGraphCompiler;
class RenderGraphExe;
class GpuScene;
enum class SceneUpdateFlags : uint32_t;
struct PassEditorContext;

class RENDERLAB_CORE_API RenderData
{
public:
    using Dictionary = std::map<std::string, uint32_t, std::less<>>;

    [[nodiscard]] Dictionary& getDictionary() const { return dictionary; }
    [[nodiscard]] glm::uvec2 getDefaultTextureDims() const { return defaultTexDims; }

    // Resources are owned by the graph.
    [[nodiscard]] rhi::IResource* getResource(std::string_view name) const;
    [[nodiscard]] rhi::ITexture*  getTexture(std::string_view name) const;
    [[nodiscard]] rhi::IBuffer*   getBuffer(std::string_view name) const;

    [[nodiscard]] uint64_t          getHistoryRevision(std::string_view name) const;
    void                            invalidateHistory(std::string_view name) const;

private:
    friend class RenderGraphExe;
    RenderData(
        const ResourceCache::Ports& ports,
        ResourceCache& resources,
        Dictionary& dictionary,
        glm::uvec2 defaultTexDims
    ) :
        ports(ports), resources(resources), dictionary(dictionary), defaultTexDims(defaultTexDims)
    {
    }

    const ResourceCache::Ports& ports;
    ResourceCache& resources;
    Dictionary& dictionary;
    glm::uvec2 defaultTexDims;

    [[nodiscard]] std::optional<size_t> findSlot(std::string_view name) const;
};

class RENDERLAB_CORE_API RenderPass
{
public:
    using PluginCreate = std::function<std::shared_ptr<RenderPass>(rhi::ComPtr<rhi::IDevice>, const nlohmann::json&)>;

    struct SettingsBinding
    {
        nlohmann::json (*get)(const RenderPass&) = nullptr;
        void (*set)(RenderPass&, const nlohmann::json&) = nullptr;
        void (*drawEditor)(const RenderPass&, const PassEditorContext&, Gui::Widgets&) = nullptr;
    };

    struct PluginInfo
    {
        std::string desc;
        std::span<const RenderPassReflection::Field> ports{};
        SettingsBinding settings{};
    };

    RL_PLUGIN_BASE_CLASS(RenderPass);

    struct CompileData
    {
        const std::string&    passName;
        ResourceCache&        resources;
        ResourceCache::Ports& ports;

        [[nodiscard]] rhi::TextureDesc getTextureDesc(std::string_view name) const;
        void addInternalTexture(std::string name, const rhi::TextureDesc& desc) const;
    };

    [[nodiscard]] static std::shared_ptr<RenderPass> create(
        std::string_view          type,
        rhi::ComPtr<rhi::IDevice> device,
        const nlohmann::json&      properties = nlohmann::json::object(),
        PluginManager&            plugins    = PluginManager::instance()
    );
    virtual ~RenderPass() = default;

    [[nodiscard]] const std::string& getType() const { return getPluginType(); }
    [[nodiscard]] const std::string& getDesc() const { return getPluginInfo().desc; }
    [[nodiscard]] uint32_t           getMaxFramesInFlight() const { return mMaxFramesInFlight; }

    [[nodiscard]] virtual RenderPassReflection reflect(const ResourceCache::DefaultProperties& defaults);
    virtual void                               compile(RenderContext* context, const CompileData& compileData) {}
    virtual void                               execute(RenderContext* context, const RenderData& renderData) = 0;
    virtual void                               setScene(GpuScene* scene) { mpScene = scene; }
    virtual void                               onSceneUpdates(RenderContext* context, SceneUpdateFlags updates) {}

    void                         setProperties(const nlohmann::json& properties);
    [[nodiscard]] nlohmann::json getProperties() const;

    [[nodiscard]] const std::string& getName() const { return mName; }

protected:
    explicit RenderPass(rhi::ComPtr<rhi::IDevice> device, uint32_t maxFramesInFlight = kMaxFramesInFlight) :
        mpDevice(std::move(device)), mMaxFramesInFlight(maxFramesInFlight)
    {
    }
    void requestRecompile() { mPassChangedCB(); }

    rhi::ComPtr<rhi::IDevice> mpDevice;
    GpuScene*                mpScene = nullptr;
    std::string               mName;
    std::function<void()>     mPassChangedCB = [] {};

    friend class RenderGraph;

private:
    uint32_t    mMaxFramesInFlight;
};
