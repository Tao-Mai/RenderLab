#include "render/PipelineManager.h"
#include "render/feature/RenderGraph.h"
#include "render/feature/shadow/ShadowData.h"

#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

TEST(SpotShadow, PreparesActiveLayersAndConeProjection)
{
    LightData source;
    source.flags          = {static_cast<uint32_t>(GpuLightType::Spot), 1u, 1u, 0u};
    source.positionRange  = {0, 0, 3, 10};
    source.direction      = {0, 0, -1, 0};
    source.areaSizeCone.z = std::cos(glm::radians(15.0f));
    source.areaSizeCone.w = std::cos(glm::radians(30.0f));
    std::array lights{source, source, source, source};
    lights[0].flags.y   = 0;
    lights[2].flags.z   = 0;
    const auto prepared = prepareShadows({}, lights, {});
    ASSERT_EQ(prepared.spotShadows.size(), 2u);
    EXPECT_EQ(prepared.lights[0].shadowidx, ~0u);
    EXPECT_EQ(prepared.lights[1].shadowidx, 0u);
    EXPECT_EQ(prepared.lights[2].shadowidx, ~0u);
    EXPECT_EQ(prepared.lights[3].shadowidx, 1u);
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
    std::array<LightData, kMaxSpotLightShadow + 1> lights;
    for (auto& light : lights)
    {
        light.flags     = {static_cast<uint32_t>(GpuLightType::Spot), 1u, 1u, 0u};
        light.direction = {0, 0, -1, 0};
    }
    EXPECT_DEATH(prepareShadows({}, lights, {}), "");
}

TEST(LightGroups, PreservesTypeOrderAndEmptyRangesWithoutChangingSource)
{
    std::array<LightData, 6> lights;
    const std::array types = {GpuLightType::Spot, GpuLightType::Point, GpuLightType::RectArea,
                             GpuLightType::Directional, GpuLightType::Point, GpuLightType::Spot};
    for (size_t index = 0; index < lights.size(); ++index)
    {
        lights[index].flags.x = static_cast<uint32_t>(types[index]);
        lights[index].positionRange.x = static_cast<float>(index);
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
