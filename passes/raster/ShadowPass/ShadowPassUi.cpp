#include "passes/raster/ShadowPass/ShadowPass.h"

#include "editor/EditorCameraConfig.h"
#include "editor/PassEditor.h"
#include "render/device/VulkanContext.h"

#include <array>
#include <span>
#include <string>
#include <utility>

namespace
{
bool drawShadowMapSize(Gui::Widgets& widgets, const char* label, uint32_t& mapSize,
                       const vk::PhysicalDeviceLimits& limits, bool cube)
{
    constexpr std::array kShadowMapSizes{
        std::pair{256u, "256"},
        std::pair{512u, "512"},
        std::pair{1024u, "1024"},
        std::pair{2048u, "2048"},
        std::pair{4096u, "4096"},
        std::pair{8192u, "8192"},
    };
    std::array<const char*, kShadowMapSizes.size()> labels;
    std::array<uint32_t, kShadowMapSizes.size()> sizes;
    size_t choiceCount = 0;
    size_t selected = sizes.size();
    for (const auto& [size, option] : kShadowMapSizes)
    {
        if ((cube && size > limits.maxImageDimensionCube) || size > limits.maxImageDimension2D ||
            size > limits.maxFramebufferWidth || size > limits.maxFramebufferHeight)
            continue;

        if (mapSize == size)
            selected = choiceCount;
        sizes[choiceCount] = size;
        labels[choiceCount] = option;
        ++choiceCount;
    }

    const std::string shadowSize = std::to_string(mapSize);
    if (!widgets.dropdown(label, std::span(labels).first(choiceCount), selected, shadowSize.c_str()))
        return false;

    mapSize = sizes[selected];
    return true;
}

bool drawCascadeSettings(Gui::Widgets& widgets, ShadowSettings::Cascade& cascade, float nearPlane)
{
    bool edited = widgets.slider("Shadow cascades", cascade.count, 1u, kMaxDirectionalShadowCascades, false);
    const float minimumShadowDistance = nearPlane * 1.01f;
    edited |= widgets.drag("Directional shadow distance", cascade.maxDistance, 0.5f, minimumShadowDistance,
                           100000.0f, "%.2f");

    edited |= widgets.dropdown("Cascade split strategy", cascade.splitStrategy, {"Practical (uniform/log)", "Uniform"});
    if (cascade.splitStrategy == CascadeSplitStrategy::Practical)
        edited |= widgets.slider("Cascade split lambda", cascade.splitLambda, 0.0f, 1.0f, "%.2f");

    edited |= widgets.slider("Cascade blend ratio", cascade.blendRatio, 0.0f, 0.5f, "%.2f");
    widgets.tooltip("Blend the last fraction of each cascade into the next. 0 disables blending.");
    edited |= widgets.drag("Directional PCF radius (texels)", cascade.sampleRadius, 0.1f, 0.0f, 32.0f, "%.2f");
    return edited;
}

bool drawSettings(Gui::Widgets& widgets, ShadowSettings& shadow, const ShadowPass&, const PassEditorContext& context)
{
    const auto& limits = context.vulkan.properties().limits;
    const auto nearPlane = context.camera.nearPlane;

    bool edited = widgets.dropdown("Shadow filter", shadow.filter, {"Nearest", "Linear"});
    edited |= drawShadowMapSize(widgets, "Point shadow map size", shadow.pointShadowMapSize, limits, true);
    edited |= drawShadowMapSize(widgets, "Spot shadow map size", shadow.spotShadowMapSize, limits, false);
    edited |= drawShadowMapSize(widgets, "Rect area shadow map size", shadow.rectAreaShadowMapSize, limits, false);
    edited |= drawShadowMapSize(widgets, "Directional shadow map size", shadow.cascade.shadowMapSize, limits, false);
    edited |= drawCascadeSettings(widgets, shadow.cascade, nearPlane);

    edited |= widgets.drag("Shadow bias", shadow.bias, 0.0001f, 0.0f, 1.0f, "%.5f");
    edited |= widgets.checkbox("Per-pixel rotation", shadow.pixelRotation);
    edited |= widgets.drag("PCF radius", shadow.sampleRadius, 0.0005f, 0.0f, 1.0f, "%.4f");
    edited |= widgets.drag("Spot PCF radius (texels)", shadow.spotSampleRadius, 0.1f, 0.0f, 32.0f, "%.2f");
    return edited;
}
}

extern "C" RENDERLAB_PLUGIN_EXPORT void registerPlugin(PluginRegistry& registry)
{
    registry.registerClass<ShadowPass>("ShadowPass", makePassEditor<ShadowPass, drawSettings>("Shadows"));
}
