#include "core/profiling/Profiler.h"

#include <gtest/gtest.h>

TEST(Profiler, CapturesCpuGpuDrawsAndCulling)
{
    Profiler profiler;
    profiler.beginFrame();
    profiler.recordCpu("Scene tick", 2.0);
    profiler.recordCpu("Scene tick", 1.0);
    profiler.recordGpu(profiler.frameNumber(), 4.0, {{"ScenePass", 3.5}, {"EndPass", 0.5}});
    profiler.beginPass("ScenePass");
    recordDraw(&profiler, 36);
    recordDraw(&profiler, 18, 2);
    profiler.beginPass("EditorUiPass");
    recordDraw(&profiler, 15);
    recordDraw(&profiler, 15);
    recordDraw(&profiler, 15);
    profiler.recordCulling(10, {{"Main view", 4}, {"Point shadow 0", 7}});
    profiler.endFrame(16.0);

    const auto& stats = profiler.display();
    EXPECT_DOUBLE_EQ(stats.frameMilliseconds, 16.0);
    EXPECT_DOUBLE_EQ(stats.p95Milliseconds, 16.0);
    EXPECT_DOUBLE_EQ(stats.p99Milliseconds, 16.0);
    ASSERT_EQ(stats.cpuTimings.size(), 1u);
    EXPECT_DOUBLE_EQ(stats.cpuTimings[0].milliseconds, 3.0);
    EXPECT_TRUE(stats.hasGpuTimings);
    EXPECT_DOUBLE_EQ(stats.gpuMilliseconds, 4.0);
    ASSERT_EQ(stats.gpuTimings.size(), 2u);
    ASSERT_EQ(stats.passes.size(), 2u);
    EXPECT_EQ(stats.passes[0].draws.drawCalls, 2u);
    EXPECT_EQ(stats.passes[0].draws.triangles, 24u);
    EXPECT_EQ(stats.passes[1].draws.drawCalls, 3u);
    EXPECT_EQ(stats.passes[1].draws.triangles, 15u);
    EXPECT_EQ(stats.totalDrawItems, 10u);
    EXPECT_EQ(stats.views[0].visibleDrawItems, 4u);
    profiler.clearGpu();
    EXPECT_FALSE(profiler.display().hasGpuTimings);
    EXPECT_TRUE(profiler.display().passes.empty());
    profiler.reset();
    EXPECT_TRUE(profiler.display().frameHistory.empty());
}

TEST(Profiler, ScopeUsesTimerAndIgnoresCallsOutsideFrames)
{
    Profiler profiler;
    profiler.recordCpu("Ignored", 100);
    profiler.beginFrame();
    {
        CpuProfileScope scope{&profiler, "Work"};
    }
    profiler.endFrame(10.0);
    ASSERT_EQ(profiler.display().cpuTimings.size(), 1u);
    EXPECT_EQ(profiler.display().cpuTimings.front().name, "Work");
    EXPECT_GE(profiler.display().cpuTimings.front().milliseconds, 0.0);
    Timer timer;
    EXPECT_GE(timer.elapsedMilliseconds(), 0.0);
    EXPECT_GE(timer.restartMilliseconds(), 0.0);
}
