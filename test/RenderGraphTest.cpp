#include "render/PipelineManager.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowData.h"
#include "scene/system/SceneRenderData.h"

#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

TEST(SpotShadow, PreparesActiveLayersAndConeProjection)
{
    RenderLight source;
    source.data.lightType = GpuLightType::Spot;
    source.data.shadowEnabled = 1;
    source.data.positionRange  = {0, 0, 3, 10};
    source.data.direction      = {0, 0, -1, 0};
    source.data.areaSizeCone.z = std::cos(glm::radians(15.0f));
    source.data.areaSizeCone.w = std::cos(glm::radians(30.0f));
    std::array lights{source, source, source, source};
    lights[0].enabled = false;
    lights[2].data.shadowEnabled = 0;
    const auto prepared = prepareShadows({}, lights, {});
    ASSERT_EQ(prepared.spotShadows.size(), 2u);
    ASSERT_EQ(prepared.lights.size(), 3u);
    EXPECT_EQ(prepared.lights[0].shadowidx, 0u);
    EXPECT_EQ(prepared.lights[1].shadowidx, ~0u);
    EXPECT_EQ(prepared.lights[2].shadowidx, 1u);
    const auto center = prepared.spotShadows[0].vp * glm::vec4(0, 0, 1, 1);
    EXPECT_NEAR(center.x, 0.0f, 0.0001f);
    EXPECT_NEAR(center.y, 0.0f, 0.0001f);
    EXPECT_GT(center.z, 0.0f);
    EXPECT_LT(center.z, center.w);
    const auto edge = prepared.spotShadows[0].vp * glm::vec4(2 * std::tan(glm::radians(30.0f)), 0, 1, 1);
    EXPECT_NEAR(edge.x / edge.w, 1.0f, 0.0001f);
    const auto disabled = prepareShadows({}, lights, {.enable = false});
    EXPECT_TRUE(disabled.spotShadows.empty());
    for (const auto& light : disabled.lights)
        EXPECT_FALSE(light.castShadow());
}

TEST(SpotShadow, RejectsCapacityOverflow)
{
    std::array<RenderLight, kMaxSpotLightShadow + 1> lights;
    for (auto& light : lights)
    {
        light.data.lightType = GpuLightType::Spot;
        light.data.shadowEnabled = 1;
        light.data.direction = {0, 0, -1, 0};
    }
    EXPECT_DEATH(prepareShadows({}, lights, {}), "");
}

TEST(LightGroups, PreservesTypeOrderAndEmptyRangesWithoutChangingSource)
{
    std::array<RenderLight, 6> lights;
    const std::array types = {GpuLightType::Spot, GpuLightType::Point, GpuLightType::RectArea,
                             GpuLightType::Directional, GpuLightType::Point, GpuLightType::Spot};
    for (size_t index = 0; index < lights.size(); ++index)
    {
        lights[index].data.lightType = types[index];
        lights[index].data.positionRange.x = static_cast<float>(index);
    }
    const auto source = lights;
    const auto prepared = prepareShadows({}, lights, {.enable = false});
    EXPECT_EQ(lights, source);
    EXPECT_EQ(prepared.lightRanges, (LightRanges{1, 3, 4, 6}));
    const std::array expectedSourceIndices = {3u, 1u, 4u, 2u, 0u, 5u};
    for (size_t index = 0; index < lights.size(); ++index)
        EXPECT_EQ(prepared.lights[index].positionRange.x, static_cast<float>(expectedSourceIndices[index]));

    const auto empty = prepareShadows({}, {}, {.enable = false});
    EXPECT_EQ(empty.lightRanges, LightRanges{});
    const auto onlySpot = prepareShadows({}, std::span{lights}.first(1), {.enable = false});
    EXPECT_EQ(onlySpot.lightRanges, (LightRanges{0, 0, 0, 1}));

    lights[1].enabled = false;
    lights[3].enabled = false;
    lights[5].enabled = false;
    const auto submitted = lights;
    const auto filtered = prepareShadows({}, lights, {.enable = false});
    EXPECT_EQ(lights, submitted);
    ASSERT_EQ(filtered.lights.size(), 3u);
    EXPECT_EQ(filtered.lightRanges, (LightRanges{0, 1, 2, 3}));
    EXPECT_FLOAT_EQ(filtered.lights[0].positionRange.x, 4.0f);
    EXPECT_FLOAT_EQ(filtered.lights[1].positionRange.x, 2.0f);
    EXPECT_FLOAT_EQ(filtered.lights[2].positionRange.x, 0.0f);

    for (auto& light : lights)
        light.enabled = false;
    const auto allDisabled = prepareShadows({}, lights, {});
    EXPECT_TRUE(allDisabled.lights.empty());
    EXPECT_EQ(allDisabled.lightRanges, LightRanges{});
}

TEST(PipelineWinding, PreservesCcwFrontFacesWithVulkanProjection)
{
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    const glm::vec3 vertices[] = {{-1.0f, -1.0f, -3.0f}, {1.0f, -1.0f, -3.0f}, {1.0f, 1.0f, -3.0f}};
    glm::vec2       projected[3];
    for (size_t index = 0; index < 3; ++index)
    {
        const glm::vec4 clip = projection * glm::vec4(vertices[index], 1.0f);
        projected[index]     = glm::vec2(clip) / clip.w;
    }

    const auto edgeA = projected[1] - projected[0];
    const auto edgeB = projected[2] - projected[0];
    // Vulkan's signed framebuffer area has the opposite sign to the usual 2D cross product.
    const float area = -0.5f * (edgeA.x * edgeB.y - edgeA.y * edgeB.x);
    EXPECT_GT(area, 0.0f);
    EXPECT_EQ(PipelineState{}.frontFace, vk::FrontFace::eCounterClockwise);
}

TEST(RenderGraphBarrier, CoversLayoutsAndReadAfterWrite)
{
    const RenderGraph::ImageAccess depthWrite{
        .stages = vk::PipelineStageFlagBits2::eLateFragmentTests,
        .access = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .layout = vk::ImageLayout::eDepthAttachmentOptimal,
    };
    const RenderGraph::ImageAccess sampled{
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eDepthReadOnlyOptimal,
    };
    EXPECT_TRUE(RenderGraph::needsBarrier(depthWrite, sampled));
    EXPECT_FALSE(RenderGraph::needsBarrier(sampled, sampled));
    EXPECT_TRUE(RenderGraph::needsBarrier({}, depthWrite));

    const RenderGraph::ImageAccess transferWrite{
        .stages = vk::PipelineStageFlagBits2::eTransfer,
        .access = vk::AccessFlagBits2::eTransferWrite,
    };
    const RenderGraph::ImageAccess hostRead{
        .stages = vk::PipelineStageFlagBits2::eHost,
        .access = vk::AccessFlagBits2::eHostRead,
    };
    EXPECT_TRUE(RenderGraph::needsBarrier(transferWrite, hostRead));
    EXPECT_TRUE(RenderGraph::needsBarrier(hostRead, transferWrite));
}

TEST(RenderGraphBarrier, CoversAccelerationStructureBuildAndTraversal)
{
    const RenderGraph::ImageAccess build{
        .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .access =
            vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
    };
    const RenderGraph::ImageAccess tlasRead{
        .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
    };
    const RenderGraph::ImageAccess traversal{
        .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
    };
    EXPECT_TRUE(RenderGraph::needsBarrier(build, tlasRead));
    EXPECT_TRUE(RenderGraph::needsBarrier(build, traversal));
    EXPECT_TRUE(RenderGraph::needsBarrier(traversal, build));
    EXPECT_FALSE(RenderGraph::needsBarrier(tlasRead, traversal));
}
