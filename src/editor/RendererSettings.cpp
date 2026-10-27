#include "editor/RendererSettings.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <numbers>
#include "asset/AssetManager.h"
#include "core/ConfigManager.h"
#include "core/Context.h"
#include "render/Renderer.h"
#include "render/device/VulkanContext.h"
#include "render/resources/ShaderData.h"
namespace
{
bool drawSettingsHeader(const char* label)
{
    bool        reset      = false;
    const float buttonSize = ImGui::GetFrameHeight();
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::BeginTable("Header", 2, ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, buttonSize);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SeparatorText(label);
        ImGui::TableNextColumn();
        reset = ImGui::Button("##ResetDefaults", ImVec2(buttonSize, buttonSize));
        ImGui::SetItemTooltip("Restore default parameters");

        const ImVec2    min = ImGui::GetItemRectMin();
        const ImVec2    center(min.x + buttonSize * 0.5f, min.y + buttonSize * 0.5f);
        const float     radius      = buttonSize * 0.23f;
        constexpr float kStartAngle = std::numbers::pi_v<float> * 0.15f;
        constexpr float kEndAngle   = std::numbers::pi_v<float> * 1.75f;
        const ImVec2    radial(std::cos(kEndAngle), std::sin(kEndAngle));
        const ImVec2    tangent(-radial.y, radial.x);
        const ImVec2    end(center.x + radial.x * radius, center.y + radial.y * radius);
        const float     arrowSize = radius * 0.65f;
        const ImU32     color     = ImGui::GetColorU32(ImGuiCol_Text);
        auto*           drawList  = ImGui::GetWindowDrawList();
        drawList->PathArcTo(center, radius, kStartAngle, kEndAngle);
        drawList->PathStroke(color, 0, std::max(1.0f, buttonSize * 0.06f));
        drawList->AddTriangleFilled(
            ImVec2(end.x + tangent.x * arrowSize, end.y + tangent.y * arrowSize),
            ImVec2(end.x + radial.x * arrowSize, end.y + radial.y * arrowSize),
            ImVec2(end.x - radial.x * arrowSize, end.y - radial.y * arrowSize),
            color
        );
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return reset;
}

bool drawEnvironmentSelection(SceneAsset& scene, AssetManager& assets)
{
    bool edited = false;
    if (drawSettingsHeader("Environment") && scene.environmentMap)
    {
        scene.environmentMap = SceneAsset{}.environmentMap;
        edited               = true;
    }
    auto&       current = scene.environmentMap;
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
                edited  = true;
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
    bool          sceneEdited = false;
    RenderFeature feature     = config.rendererConfig().feature;
    if (drawSettingsHeader("Renderer"))
    {
        const RendererConfig defaults;
        config.setRenderPath(defaults.renderPath);
        feature.ibl                = defaults.feature.ibl;
        feature.ambientCoefficient = defaults.feature.ambientCoefficient;
        config.setRenderFeature(feature);
    }
    const RenderPath     path = config.rendererConfig().renderPath;
    constexpr std::array kRenderPaths{
        std::pair{RenderPath::Forward, "Forward"},
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
            ImGui::BeginDisabled(
                (candidate == RenderPath::RayTracing || candidate == RenderPath::OfflinePathTracing) && !supported
            );
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

    if (drawSettingsHeader("HDR"))
    {
        feature.hdr         = RenderFeature{}.hdr;
        feature.toneMapping = {};
        config.setRenderFeature(feature);
    }
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

    if (drawSettingsHeader("Bloom"))
    {
        feature.bloom = {};
        config.setRenderFeature(feature);
    }
    ImGui::BeginDisabled(!feature.hdr);
    if (ImGui::Checkbox("Enable bloom", &feature.bloom.enable))
        config.setRenderFeature(feature);
    ImGui::SetItemTooltip("HDR scattering bloom; light markers are included, editor UI is excluded. Requires HDR.");
    ImGui::BeginDisabled(!feature.bloom.enable);
    bool bloomEdited = ImGui::SliderFloat("Bloom intensity", &feature.bloom.intensity, 0.0f, 1.0f, "%.3f");
    ImGui::SetItemTooltip("Fraction of scene light redistributed by bloom, not additive brightness.");
    bloomEdited = ImGui::SliderFloat("Bloom scatter", &feature.bloom.scatter, 0.05f, 0.95f, "%.2f") || bloomEdited;
    ImGui::SetItemTooltip("Higher values give coarser pyramid levels more weight and produce a wider glow.");
    bloomEdited = ImGui::DragFloat(
                      "Gaussian radius",
                      &feature.bloom.filterRadius,
                      0.05f,
                      0.1f,
                      5.0f,
                      "%.2f",
                      ImGuiSliderFlags_AlwaysClamp
                  ) ||
        bloomEdited;
    int bloomLevels = static_cast<int>(feature.bloom.levels);
    if (ImGui::SliderInt("Bloom levels", &bloomLevels, 1, static_cast<int>(kMaxBloomLevels)))
    {
        feature.bloom.levels = static_cast<uint32_t>(bloomLevels);
        bloomEdited          = true;
    }
    if (bloomEdited)
        config.setRenderFeature(feature);
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (config.rendererConfig().renderPath == RenderPath::OfflinePathTracing)
    {
        if (drawSettingsHeader("Offline path tracing"))
        {
            feature.offline = {};
            config.setRenderFeature(feature);
        }
        ImGui::Text("Samples per pixel: %u", context().renderer->offlineSampleCount());
        int maxBounces = static_cast<int>(feature.offline.maxBounces);
        if (ImGui::SliderInt(
                "Max bounces",
                &maxBounces,
                static_cast<int>(kMinPathBounces),
                static_cast<int>(kMaxPathBounces),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.offline.maxBounces = static_cast<uint32_t>(maxBounces);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");
        ImGui::TextUnformatted("1 sample/pixel per frame; scene/camera/bounce changes reset accumulation.");
        return sceneEdited;
    }

    if (config.rendererConfig().renderPath == RenderPath::RayTracing)
    {
        if (drawSettingsHeader("Ray tracing"))
        {
            feature.rayTracing = {};
            config.setRenderFeature(feature);
        }
        int maxBounces = static_cast<int>(feature.rayTracing.maxBounces);
        if (ImGui::SliderInt(
                "Max bounces",
                &maxBounces,
                static_cast<int>(kMinPathBounces),
                static_cast<int>(kMaxPathBounces),
                "%d",
                ImGuiSliderFlags_AlwaysClamp
            ))
        {
            feature.rayTracing.maxBounces = static_cast<uint32_t>(maxBounces);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip("Maximum surface scattering events. 1 gives direct lighting plus terminal emission.");

        if (ImGui::Checkbox("Enable SVGF", &feature.rayTracing.svgf))
            config.setRenderFeature(feature);
        ImGui::SetItemTooltip("Temporally accumulates direct and indirect lighting, estimates variance and applies\n"
                              "five edge-aware wavelet levels. The final level combines the result with emission.");

        ImGui::BeginDisabled(!feature.rayTracing.svgf);
        int historyLength = static_cast<int>(feature.rayTracing.historyLength);
        if (ImGui::SliderInt("History length", &historyLength, 1, static_cast<int>(kMaxSvgfHistoryLength),
                             "%d", ImGuiSliderFlags_AlwaysClamp))
        {
            feature.rayTracing.historyLength = static_cast<uint32_t>(historyLength);
            config.setRenderFeature(feature);
        }
        ImGui::SetItemTooltip("Average the first N samples arithmetically; use EMA when the sample count exceeds N.\n"
                              "Applies to color and both luminance moments for direct and indirect lighting.");
        if (ImGui::SliderFloat(
                "EMA sample weight",
                &feature.rayTracing.emaAlpha,
                0.0f,
                1.0f,
                "%.3f",
                ImGuiSliderFlags_AlwaysClamp
            ))
            config.setRenderFeature(feature);
        ImGui::SetItemTooltip("After history length: new mean = history * (1 - weight) + current sample * weight.\n"
                              "The first sample, or a sample with rejected history, has weight 1.");
        ImGui::EndDisabled();
        return sceneEdited;
    }

    if (drawSettingsHeader("Shadows"))
    {
        feature.shadow = {};
        config.setRenderFeature(feature);
    }
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
    drawShadowMapSize("Spot shadow map size", feature.shadow.spotShadowMapSize, false);
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
    int splitStrategy = static_cast<int>(feature.shadow.cascade.splitStrategy);
    if (ImGui::Combo("Cascade split strategy", &splitStrategy, "Practical (uniform/log)\0Uniform\0"))
    {
        feature.shadow.cascade.splitStrategy = static_cast<CascadeSplitStrategy>(splitStrategy);
        config.setRenderFeature(feature);
    }
    if (feature.shadow.cascade.splitStrategy == CascadeSplitStrategy::Practical)
        if (ImGui::SliderFloat("Cascade split lambda", &feature.shadow.cascade.splitLambda, 0.0f, 1.0f, "%.2f"))
            config.setRenderFeature(feature);
    if (ImGui::SliderFloat("Cascade blend ratio", &feature.shadow.cascade.blendRatio, 0.0f, 0.5f, "%.2f"))
        config.setRenderFeature(feature);
    ImGui::SetItemTooltip("Blend the last fraction of each cascade into the next. 0 disables blending.");
    if (ImGui::DragFloat(
            "Directional PCF radius (texels)",
            &feature.shadow.cascade.sampleRadius,
            0.1f,
            0.0f,
            32.0f,
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
    if (ImGui::DragFloat(
            "Spot PCF radius (texels)",
            &feature.shadow.spotSampleRadius,
            0.1f,
            0.0f,
            32.0f,
            "%.2f",
            ImGuiSliderFlags_AlwaysClamp
        ))
        config.setRenderFeature(feature);
    ImGui::EndDisabled();
    return sceneEdited;
}
