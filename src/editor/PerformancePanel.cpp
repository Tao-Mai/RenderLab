#include "editor/PerformancePanel.h"
#include "core/profiling/Profiler.h"

#include <cfloat>
#include <imgui.h>

namespace
{
void drawTimings(const char* id, const std::vector<ProfileTiming>& timings)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("Scope");
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();
    for (const auto& timing : timings)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(timing.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", timing.milliseconds);
    }
    ImGui::EndTable();
}
}  // namespace

void drawPerformance(const Profiler* profiler)
{
    ImGui::SeparatorText("Performance");
    if (!profiler)
    {
        ImGui::TextDisabled("Profiler unavailable");
        return;
    }
    const auto& stats = profiler->display();
    ImGui::Text(
        "FPS: %.1f  |  Frame: %.2f ms",
        stats.frameMilliseconds > 0 ? 1000.0 / stats.frameMilliseconds : 0.0,
        stats.frameMilliseconds
    );
    ImGui::Text("CPU frame (avg): %.2f ms", stats.cpuMilliseconds);
    if (stats.hasGpuTimings)
        ImGui::Text("GPU frame: %.2f ms", stats.gpuMilliseconds);
    else
        ImGui::TextDisabled("GPU timings pending / unsupported");

    if (!ImGui::TreeNode("Performance details"))
        return;
    ImGui::Text("P95: %.2f ms  |  P99: %.2f ms", stats.p95Milliseconds, stats.p99Milliseconds);
    if (!stats.frameHistory.empty())
        ImGui::PlotLines(
            "##Frame history",
            stats.frameHistory.data(),
            static_cast<int>(stats.frameHistory.size()),
            0,
            "Frame time (ms)",
            0.0f,
            FLT_MAX,
            ImVec2{0, 65}
        );
    if (ImGui::TreeNode("CPU scopes"))
    {
        ImGui::TextDisabled("Last sampled frame; nested scopes overlap.");
        drawTimings("CPU timings", stats.cpuTimings);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("GPU passes"))
    {
        ImGui::TextDisabled("Last completed frame; nested scopes overlap; total is measured separately.");
        drawTimings("GPU timings", stats.gpuTimings);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Draw submissions"))
    {
        DrawStatistics total;
        if (ImGui::BeginTable("Draw counts", 3, ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Pass");
            ImGui::TableSetupColumn("Draws");
            ImGui::TableSetupColumn("Triangles");
            ImGui::TableHeadersRow();
            for (const auto& pass : stats.passes)
            {
                total.drawCalls += pass.draws.drawCalls;
                total.triangles += pass.draws.triangles;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(pass.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(pass.draws.drawCalls));
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(pass.draws.triangles));
            }
            ImGui::EndTable();
        }
        ImGui::Text(
            "Total: %llu draws, %llu triangles",
            static_cast<unsigned long long>(total.drawCalls),
            static_cast<unsigned long long>(total.triangles)
        );
        ImGui::TextDisabled("Before multiview amplification; includes editor draws.");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Frustum culling"))
    {
        ImGui::Text("Render objects: %u", stats.totalObjects);
        if (ImGui::BeginTable("Culling counts", 3, ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("View");
            ImGui::TableSetupColumn("Visible");
            ImGui::TableSetupColumn("Culled");
            ImGui::TableHeadersRow();
            for (const auto& view : stats.views)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(view.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", view.visibleObjects);
                ImGui::TableNextColumn();
                ImGui::Text("%u", stats.totalObjects - view.visibleObjects);
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Camera visibility does not cull ray tracing geometry.");
        ImGui::TreePop();
    }
    ImGui::TreePop();
}
