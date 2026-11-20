#include "passes/raster/ShadowPass/ShadowPass.h"

#include "editor/PassEditorRegistry.h"
#include "editor/SettingsWidgets.h"

#include "editor/EditorCameraConfig.h"
#include "render/device/VulkanContext.h"

#include <string>

namespace
{
bool drawShadowMapSize(const char* label, uint32_t& mapSize, const vk::PhysicalDeviceLimits& limits, bool cube)
{
    const std::string shadowSize = std::to_string(mapSize);
    if (!ImGui::BeginCombo(label, shadowSize.c_str()))
        return false;

    constexpr uint32_t kShadowMapSizes[] = {256, 512, 1024, 2048, 4096, 8192};
    bool               edited            = false;
    for (uint32_t size : kShadowMapSizes)
    {
        if ((cube && size > limits.maxImageDimensionCube) || size > limits.maxImageDimension2D ||
            size > limits.maxFramebufferWidth || size > limits.maxFramebufferHeight)
            continue;

        const bool        selected = mapSize == size;
        const std::string option   = std::to_string(size);
        if (ImGui::Selectable(option.c_str(), selected) && !selected)
        {
            mapSize = size;
            edited  = true;
        }
    }
    ImGui::EndCombo();
    return edited;
}

bool drawCascadeSettings(ShadowSettings::Cascade& cascade, float nearPlane)
{
    bool        edited = drawUnsignedSlider("Shadow cascades", cascade.count, 1, kMaxDirectionalShadowCascades);
    const float minimumShadowDistance = nearPlane * 1.01f;
    edited |= ImGui::DragFloat(
        "Directional shadow distance",
        &cascade.maxDistance,
        0.5f,
        minimumShadowDistance,
        100000.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );

    edited |= drawEnumCombo("Cascade split strategy", cascade.splitStrategy, "Practical (uniform/log)\0Uniform\0");
    if (cascade.splitStrategy == CascadeSplitStrategy::Practical)
        edited |= ImGui::SliderFloat("Cascade split lambda", &cascade.splitLambda, 0.0f, 1.0f, "%.2f");

    edited |= ImGui::SliderFloat("Cascade blend ratio", &cascade.blendRatio, 0.0f, 0.5f, "%.2f");
    ImGui::SetItemTooltip("Blend the last fraction of each cascade into the next. 0 disables blending.");
    edited |= ImGui::DragFloat(
        "Directional PCF radius (texels)",
        &cascade.sampleRadius,
        0.1f,
        0.0f,
        32.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );
    return edited;
}

bool drawSettings(ShadowSettings& shadow, const ShadowPass&, const PassEditorContext& context)
{
    const auto& limits = context.vulkan.properties().limits;
    const auto nearPlane = context.camera.nearPlane;

    bool edited = false;
    edited |= drawEnumCombo("Shadow filter", shadow.filter, "Nearest\0Linear\0");

    edited |= drawShadowMapSize("Point shadow map size", shadow.pointShadowMapSize, limits, true);
    edited |= drawShadowMapSize("Spot shadow map size", shadow.spotShadowMapSize, limits, false);
    edited |= drawShadowMapSize("Rect area shadow map size", shadow.rectAreaShadowMapSize, limits, false);
    edited |= drawShadowMapSize("Directional shadow map size", shadow.cascade.shadowMapSize, limits, false);
    edited |= drawCascadeSettings(shadow.cascade, nearPlane);

    edited |= ImGui::DragFloat("Shadow bias", &shadow.bias, 0.0001f, 0.0f, 1.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
    edited |= ImGui::Checkbox("Per-pixel rotation", &shadow.pixelRotation);
    edited |=
        ImGui::DragFloat("PCF radius", &shadow.sampleRadius, 0.0005f, 0.0f, 1.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
    edited |= ImGui::DragFloat(
        "Spot PCF radius (texels)",
        &shadow.spotSampleRadius,
        0.1f,
        0.0f,
        32.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp
    );
    return edited;
}
}

void registerShadowPassEditor(PassEditorRegistry& editors)
{
    editors.add<ShadowPass, drawSettings>("Shadows");
}
