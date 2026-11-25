#pragma once
#include <slang-rhi.h>

#include <glm/vec2.hpp>
#include "editor/Gui.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassReflection.h"
#include "render/graph/ResourceCache.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

class RenderGraphCompiler;
class RenderGraphExe;
class GpuScene;
enum class SceneUpdateFlags : uint32_t;
struct PassEditorContext;
struct RenderPassOptions;

class RenderData
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

class RenderPass
{
public:
    struct SettingsBinding
    {
        void (*apply)(RenderPass&, const RenderPassOptions&) = nullptr;
        void (*drawEditor)(const RenderPass&, const PassEditorContext&, Gui::Widgets&, RenderPassOptions&, bool) = nullptr;
    };

    struct Description
    {
        std::string_view type;
        std::span<const RenderPassReflection::Field> ports{};
        SettingsBinding settings{};
    };

    struct CompileData
    {
        const std::string&    passName;
        ResourceCache&        resources;
        ResourceCache::Ports& ports;

        [[nodiscard]] rhi::TextureDesc getTextureDesc(std::string_view name) const;
        void addInternalTexture(std::string name, const rhi::TextureDesc& desc) const;
    };

    virtual ~RenderPass() = default;

    [[nodiscard]] std::string_view getType() const { return description.type; }
    [[nodiscard]] const SettingsBinding& getSettingsBinding() const { return description.settings; }
    [[nodiscard]] uint32_t           getMaxFramesInFlight() const { return mMaxFramesInFlight; }

    [[nodiscard]] virtual RenderPassReflection reflect(const ResourceCache::DefaultProperties& defaults);
    virtual void                               compile(RenderContext* context, const CompileData& compileData) {}
    virtual void                               execute(RenderContext* context, const RenderData& renderData) = 0;
    virtual void                               setScene(GpuScene* scene) { mpScene = scene; }
    virtual void                               onSceneUpdates(RenderContext* context, SceneUpdateFlags updates) {}

    [[nodiscard]] const std::string& getName() const { return mName; }

protected:
    RenderPass(
        rhi::ComPtr<rhi::IDevice> device,
        Description description,
        uint32_t maxFramesInFlight = kMaxFramesInFlight
    ) :
        mpDevice(std::move(device)), description(description), mMaxFramesInFlight(maxFramesInFlight)
    {
    }
    void requestRecompile() { mPassChangedCB(); }

    rhi::ComPtr<rhi::IDevice> mpDevice;
    GpuScene*                mpScene = nullptr;
    std::string               mName;
    std::function<void()>     mPassChangedCB = [] {};

    friend class RenderGraph;

private:
    Description description;
    uint32_t mMaxFramesInFlight;
};
