#pragma once
#include <slang-rhi.h>

#include "core/Macros.h"
#include "render/graph/RenderPass.h"
#include "render/graph/ResourceCache.h"

#include <string_view>
#include <vector>

class RENDERLAB_CORE_API RenderGraphExe
{
public:
    struct Context
    {
        RenderContext* pRenderContext;
        Dictionary&    passesDictionary;
        glm::uvec2          defaultTexDims;
    };

    void                                      execute(const Context& context);
    void                                      renderUI(RenderContext* context, Gui::Widgets& widget);
    [[nodiscard]] rhi::ComPtr<rhi::IResource> getResource(const std::string& name) const;
    [[nodiscard]] SceneInputs                getSceneInputs() const { return mSceneInputs; }
    [[nodiscard]] uint32_t                   getMaxFramesInFlight() const { return mMaxFramesInFlight; }
    [[nodiscard]] bool                       hasPassType(std::string_view type) const;
    void                                      setInput(const std::string& name, rhi::IResource* resource);

private:
    friend class RenderGraphCompiler;
    void insertPass(const std::string& name, const ref<RenderPass>& pass);

    struct Pass
    {
        std::string     name;
        ref<RenderPass> pPass;

    private:
        friend class RenderGraphExe;
        Pass(const std::string& name, const ref<RenderPass>& pass) : name(name), pPass(pass) {}
    };

    std::vector<Pass>              mExecutionList;
    std::unique_ptr<ResourceCache> mpResourceCache;
    SceneInputs                    mSceneInputs = SceneInputs::None;
    uint32_t                       mMaxFramesInFlight = kMaxFramesInFlight;
};
