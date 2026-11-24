#pragma once
#include <slang-rhi.h>

#include "core/Dictionary.h"
#include "core/HotReloadFlags.h"
#include "core/Macros.h"
#include "core/Object.h"
#include "core/Plugin.h"
#include "core/Properties.h"
#include "core/math/Vector.h"
#include "editor/Gui.h"
#include "render/RenderContext.h"
#include "render/graph/RenderPassReflection.h"
#include "scene/IScene.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace Falcor
{
class Scene;
struct KeyboardEvent;
struct MouseEvent;

class RenderGraphCompiler;
class RenderGraphExe;
class ResourceCache;

class FALCOR_API RenderData
{
public:
    [[nodiscard]] const rhi::ComPtr<rhi::IResource>& getResource(std::string_view name) const;
    [[nodiscard]] rhi::ComPtr<rhi::ITexture>         getTexture(std::string_view name) const;
    [[nodiscard]] const rhi::ComPtr<rhi::IResource>& operator[](std::string_view name) const
    {
        return getResource(name);
    }
    [[nodiscard]] rhi::ComPtr<rhi::IBuffer> getBuffer(std::string_view name) const;
    void                                    setBuffer(std::string_view name, rhi::IBuffer* buffer) const;
    [[nodiscard]] Dictionary&               getDictionary() const { return mDictionary; }
    [[nodiscard]] const uint2&              getDefaultTextureDims() const { return mDefaultTexDims; }

protected:
    friend class RenderGraphExe;
    RenderData(
        const std::string& passName,
        ResourceCache&     resources,
        Dictionary&        dictionary,
        const uint2&       defaultTexDims
    );

    const std::string& mName;
    ResourceCache&     mResources;
    Dictionary&        mDictionary;
    uint2              mDefaultTexDims;
};

class FALCOR_API RenderPass : public Object
{
    FALCOR_OBJECT(RenderPass)

public:
    using PluginCreate = std::function<ref<RenderPass>(rhi::ComPtr<rhi::IDevice>, const Properties&)>;

    struct PluginInfo
    {
        std::string desc;
    };

    FALCOR_PLUGIN_BASE_CLASS(RenderPass);

    struct CompileData
    {
        uint2                defaultTexDims{0};
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

    [[nodiscard]] virtual RenderPassReflection reflect(const CompileData& compileData) = 0;
    virtual void                               compile(RenderContext* context, const CompileData& compileData) {}
    virtual void                               execute(RenderContext* context, const RenderData& renderData) = 0;

    virtual void                     setProperties(const Properties& properties);
    [[nodiscard]] virtual Properties getProperties() const { return {}; }
    virtual void                     renderUI(Gui::Widgets& widget) {}
    virtual void                     renderUI(RenderContext* context, Gui::Widgets& widget) { renderUI(widget); }
    virtual void                     renderOverlayUI(RenderContext* context) {}
    virtual void                     setScene(const ref<Scene>& scene) {}
    virtual void                     onSceneUpdates(RenderContext* context, IScene::UpdateFlags updates) {}
    virtual bool                     onMouseEvent(const MouseEvent& event) { return false; }
    virtual bool                     onKeyEvent(const KeyboardEvent& event) { return false; }
    virtual void                     onHotReload(HotReloadFlags reloaded) {}

    [[nodiscard]] const std::string& getName() const { return mName; }

protected:
    explicit RenderPass(rhi::ComPtr<rhi::IDevice> device) : mpDevice(std::move(device)) {}
    void requestRecompile() { mPassChangedCB(); }

    rhi::ComPtr<rhi::IDevice> mpDevice;
    std::string               mName;
    std::function<void()>     mPassChangedCB = [] {};

    friend class RenderGraph;
};
}  // namespace Falcor
