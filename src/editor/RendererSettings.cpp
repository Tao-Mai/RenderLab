#include "editor/RendererSettings.h"
#include <algorithm>
#include <array>
#include <imgui.h>
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/Renderer.h"
#include "render/device/VulkanContext.h"
#include "render/resource/ShaderData.h"
namespace
{
bool drawEnvironmentSelection(SceneAsset& scene, AssetManager& assets)
{
    ImGui::SeparatorText("Environment");
    auto& current = scene.environmentMap;
    bool edited = false;
    const char* preview = current ? current->value.c_str() : "Select environment map...";

    if (ImGui::BeginCombo("Environment map", preview))
    {
        auto ids = assets.loadedIds<EnvironmentMapAsset>();
        std::ranges::sort(ids, {}, &EnvironmentMapAsset::ID::value);
        for (const EnvironmentMapAsset::ID& id : ids)
        {
            const bool selected = current && *current == id;
            if (ImGui::Selectable(id.value.c_str(), selected) && !selected)
            {
                current = id;
                edited = true;
            }
        }
        ImGui::EndCombo();
    }
    if (!current)
        ImGui::TextUnformatted("Using fallback environment lighting");
    return edited;
}
}  // namespace
bool drawRendererSettings(ConfigManager& config, AssetManager& assets, const VulkanContext& vulkan, SceneAsset& scene)
{
    bool sceneEdited = false;
    ImGui::SeparatorText("Renderer");
    const RenderPath     path = config.rendererConfig().renderPath;
    constexpr std::array kRenderPaths{
        std::pair{RenderPath::Rasterization, "Forward"},
        std::pair{RenderPath::Deferred, "Deferred"},
        std::pair{RenderPath::RayTracing, "Real-time ray tracing"},
        std::pair{RenderPath::OfflinePathTracing, "Offline path tracing"},
    };
    const auto selected =
        std::ranges::find_if(kRenderPaths, [path](const auto& option) { return option.first == path; });
    DCHECK(selected != kRenderPaths.end());
    if (ImGui::BeginCombo("Render path", selected->second))
    {
        const bool supported = vulkan.supportsRayTracing();
        for (const auto& [candidate, label] : kRenderPaths)
        {
            ImGui::BeginDisabled((candidate == RenderPath::RayTracing || candidate == RenderPath::OfflinePathTracing) && !supported);
            if (ImGui::Selectable(label, path == candidate) && path != candidate)
            {
                config.setRenderPath(candidate);
            }
            ImGui::EndDisabled();
        }
        if (!supported)
            ImGui::TextUnformatted("Ray tracing is not supported by this GPU");
        ImGui::EndCombo();
    }
    ImGui::Spacing();

    RenderFeature feature = config.rendererConfig().feature;
    if (ImGui::Checkbox("IBL", &feature.ibl))
    {
        config.setRenderFeature(feature);
    }
    if (feature.ibl)
    {
        sceneEdited = drawEnvironmentSelection(scene, assets);
    }
    else if (ImGui::DragFloat(
                 "Ambient coefficient",
                 &feature.ambientCoefficient,
                 0.01f,
                 0.0f,
                 1.0f,
                 "%.3f",
                 ImGuiSliderFlags_AlwaysClamp
             ))
    {
        config.setRenderFeature(feature);
    }

    ImGui::SeparatorText("HDR");
    if (ImGui::Checkbox("Enable HDR", &feature.hdr))
    {
        config.setRenderFeature(feature);
    }
    ImGui::BeginDisabled(!feature.hdr);
    int toneMappingMode = static_cast<int>(feature.toneMapping.mode);
    if (ImGui::Combo("Operator", &toneMappingMode, "Khronos PBR Neutral\0Reinhard\0"))
    {
        feature.toneMapping.mode = static_cast<ToneMappingOperator>(toneMappingMode);
        config.setRenderFeature(feature);
    }
    if (ImGui::DragFloat(
            "EV100",
            &feature.toneMapping.ev100,
            0.05f,
            -10.0f,
            30.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        config.setRenderFeature(feature);
    }
    ImGui::SetItemTooltip("Higher EV100 darkens the image; +1 halves linear brightness.");
    if (ImGui::DragFloat(
            "Exposure compensation (stops)",
            &feature.toneMapping.exposureCompensation,
            0.05f,
            -10.0f,
            10.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        config.setRenderFeature(feature);
    }
    ImGui::EndDisabled();

    if (config.rendererConfig().renderPath == RenderPath::OfflinePathTracing)
    {
        ImGui::SeparatorText("Offline path tracing");
        ImGui::Text("Samples per pixel: %u", context().renderer->offlineSampleCount());
        const auto properties = vulkan.physicalDeviceHandle().getProperties2<
            vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        const int maxDepth = static_cast<int>(properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>().maxRayRecursionDepth);
        int depth = static_cast<int>(feature.offline.maxRecursionDepth);
        if (ImGui::SliderInt("Max recursion depth", &depth, static_cast<int>(kMinRayRecursionDepth), maxDepth,
                             "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            feature.offline.maxRecursionDepth = static_cast<uint32_t>(depth);
            config.setRenderFeature(feature);
        }
        ImGui::TextUnformatted("1 sample/pixel per frame; scene/camera changes reset accumulation.");
        return sceneEdited;
    }

    if (config.rendererConfig().renderPath == RenderPath::RayTracing)
    {
        ImGui::SeparatorText("Ray tracing");
        const auto properties = vulkan.physicalDeviceHandle().getProperties2<
            vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        const auto maxDepth = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>().maxRayRecursionDepth;
        int recursionDepth = static_cast<int>(feature.rayTracing.maxRecursionDepth);
        if (ImGui::SliderInt(
                "Max recursion depth",
                &recursionDepth,
                static_cast<int>(kMinRayRecursionDepth),
                static_cast<int>(maxDepth),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.rayTracing.maxRecursionDepth = static_cast<uint32_t>(recursionDepth);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip("Includes the primary ray and one level reserved for visibility queries.\n"
                              "Depth 2: direct lighting only; depth 4: up to two diffuse bounces.");
        return sceneEdited;
    }

    ImGui::SeparatorText("Shadows");
    if (ImGui::Checkbox("Enable shadows", &feature.shadow.enable))
    {
        config.setRenderFeature(feature);
    }

    ImGui::BeginDisabled(!feature.shadow.enable);
    int shadowFilter = static_cast<int>(feature.shadow.filter);
    if (ImGui::Combo("Shadow filter", &shadowFilter, "Nearest\0Linear\0"))
    {
        feature.shadow.filter = static_cast<SamplerAsset::Filter>(shadowFilter);
        config.setRenderFeature(feature);
    }

    const auto drawShadowMapSize = [&](const char* label, uint32_t& mapSize, bool cube)
    {
        const std::string shadowSize = std::to_string(mapSize);
        if (!ImGui::BeginCombo(label, shadowSize.c_str()))
            return;

        constexpr uint32_t kShadowMapSizes[] = {256, 512, 1024, 2048, 4096, 8192};
        const auto&        limits            = vulkan.properties().limits;
        for (uint32_t size : kShadowMapSizes)
        {
            if ((cube && size > limits.maxImageDimensionCube) || size > limits.maxImageDimension2D ||
                size > limits.maxFramebufferWidth || size > limits.maxFramebufferHeight)
                continue;

            const bool        selected = mapSize == size;
            const std::string label    = std::to_string(size);
            if (ImGui::Selectable(label.c_str(), selected) && !selected)
            {
                mapSize = size;
                config.setRenderFeature(feature);
            }
        }
        ImGui::EndCombo();
    };
    drawShadowMapSize("Point shadow map size", feature.shadow.pointShadowMapSize, true);
    drawShadowMapSize("Rect area shadow map size", feature.shadow.rectAreaShadowMapSize, false);
    drawShadowMapSize("Directional shadow map size", feature.shadow.cascade.shadowMapSize, false);
    int cascadeCount = static_cast<int>(feature.shadow.cascade.count);
    if (ImGui::SliderInt("Shadow cascades", &cascadeCount, 1, static_cast<int>(kMaxDirectionalShadowCascades)))
    {
        feature.shadow.cascade.count = static_cast<uint32_t>(cascadeCount);
        config.setRenderFeature(feature);
    }
    const float minimumShadowDistance = scene.editorCamera.nearPlane * 1.01f;
    if (ImGui::DragFloat(
            "Directional shadow distance",
            &feature.shadow.cascade.maxDistance,
            0.5f,
            minimumShadowDistance,
            100000.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        config.setRenderFeature(feature);
    if (ImGui::SliderFloat("Cascade split lambda", &feature.shadow.cascade.splitLambda, 0.0f, 1.0f, "%.2f"))
        config.setRenderFeature(feature);
    if (ImGui::DragFloat(
            "Cascade depth padding",
            &feature.shadow.cascade.depthPadding,
            0.1f,
            0.0f,
            100000.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        config.setRenderFeature(feature);
    if (ImGui::DragFloat(
            "Shadow bias",
            &feature.shadow.bias,
            0.0001f,
            0.0f,
            1.0f,
            "%.5f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        config.setRenderFeature(feature);
    }
    if (ImGui::Checkbox("Per-pixel rotation", &feature.shadow.pixelRotation))
    {
        config.setRenderFeature(feature);
    }
    if (ImGui::DragFloat(
            "PCF radius",
            &feature.shadow.sampleRadius,
            0.0005f,
            0.0f,
            1.0f,
            "%.4f",
            ImGuiSliderFlags_AlwaysClamp
        ))
    {
        config.setRenderFeature(feature);
    }
    ImGui::EndDisabled();
    return sceneEdited;
}
