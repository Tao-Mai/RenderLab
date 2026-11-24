#pragma once
#include <slang-rhi.h>

#include "core/Dictionary.h"
#include "core/Macros.h"
#include "core/Object.h"
#include "core/Plugin.h"
#include "core/Properties.h"
#include <glm/vec2.hpp>
#include "editor/Gui.h"
#include "render/RenderConfig.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassReflection.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

class RenderGraphCompiler;
class RenderGraphExe;
class ResourceCache;

enum class SceneInputs : uint32_t
{
    None             = 0,
    RasterVisibility = 1u << 0,
    RayTracing       = 1u << 1,
};
RL_ENUM_CLASS_OPERATORS(SceneInputs);

class RENDERLAB_CORE_API RenderData
{
public:
    [[nodiscard]] const rhi::ComPtr<rhi::IResource>& getResource(std::string_view name) const;
    [[nodiscard]] rhi::ComPtr<rhi::ITexture>         getTexture(std::string_view name) const;
    [[nodiscard]] rhi::ComPtr<rhi::IBuffer> getBuffer(std::string_view name) const;
    void                                    setBuffer(std::string_view name, rhi::IBuffer* buffer) const;
    [[nodiscard]] Dictionary&               getDictionary() const { return mDictionary; }
    [[nodiscard]] const glm::uvec2&              getDefaultTextureDims() const { return mDefaultTexDims; }

protected:
    friend class RenderGraphExe;
    RenderData(
        const std::string& passName,
        ResourceCache&     resources,
        Dictionary&        dictionary,
        const glm::uvec2&       defaultTexDims
    );

    const std::string& mName;
    ResourceCache&     mResources;
    Dictionary&        mDictionary;
    glm::uvec2              mDefaultTexDims;
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
    [[nodiscard]] SceneInputs        getSceneInputs() const { return mSceneInputs; }
    [[nodiscard]] uint32_t           getMaxFramesInFlight() const { return mMaxFramesInFlight; }

    [[nodiscard]] virtual RenderPassReflection reflect(const CompileData& compileData) = 0;
    virtual void                               compile(RenderContext* context, const CompileData& compileData) {}
    virtual void                               execute(RenderContext* context, const RenderData& renderData) = 0;

    virtual void                     setProperties(const Properties& properties);
    [[nodiscard]] virtual Properties getProperties() const { return {}; }
    virtual void                     renderUI(RenderContext* context, Gui::Widgets& widget) {}

    [[nodiscard]] const std::string& getName() const { return mName; }

protected:
    explicit RenderPass(
        rhi::ComPtr<rhi::IDevice> device,
        SceneInputs              sceneInputs = SceneInputs::None,
        uint32_t                 maxFramesInFlight = kMaxFramesInFlight
    ) : mpDevice(std::move(device)), mSceneInputs(sceneInputs), mMaxFramesInFlight(maxFramesInFlight)
    {
    }
    void requestRecompile() { mPassChangedCB(); }

    rhi::ComPtr<rhi::IDevice> mpDevice;
    std::string               mName;
    std::function<void()>     mPassChangedCB = [] {};

    friend class RenderGraph;

private:
    SceneInputs mSceneInputs;
    uint32_t    mMaxFramesInFlight;
};
