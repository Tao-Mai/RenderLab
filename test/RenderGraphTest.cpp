#include "render/device/Device.h"
#include "render/graph/RenderGraph.h"
#include "render/raster/ShadowData.h"
#include "scene/system/SceneRenderData.h"

#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

TEST(SpotShadow, PreparesActiveLayersAndConeProjection)
{
    LightData source;
    source.lightType      = GpuLightType::Spot;
    source.shadowEnabled  = 1;
    source.shadowIdx      = 0;
    source.positionRange  = {0, 0, 3, 10};
    source.direction      = {0, 0, -1, 0};
    source.areaSizeCone.z = std::cos(glm::radians(15.0f));
    source.areaSizeCone.w = std::cos(glm::radians(30.0f));
    std::array lights{source, source, source};
    lights[1].shadowEnabled   = 0;
    lights[1].shadowIdx       = ~0u;
    lights[2].shadowIdx       = 1;
    const auto originalLights = lights;
    const auto prepared       = prepareShadows({}, lights, {});
    ASSERT_EQ(prepared.spotShadows.size(), 2u);
    EXPECT_EQ(lights, originalLights);
    const auto center = prepared.spotShadows[0].vp * glm::vec4(0, 0, 1, 1);
    EXPECT_NEAR(center.x, 0.0f, 0.0001f);
    EXPECT_NEAR(center.y, 0.0f, 0.0001f);
    EXPECT_GT(center.z, 0.0f);
    EXPECT_LT(center.z, center.w);
    const auto edge = prepared.spotShadows[0].vp * glm::vec4(2 * std::tan(glm::radians(30.0f)), 0, 1, 1);
    EXPECT_NEAR(edge.x / edge.w, 1.0f, 0.0001f);
    for (auto& light : lights)
        light.shadowEnabled = 0;
    const auto disabled = prepareShadows({}, lights, {});
    EXPECT_TRUE(disabled.spotShadows.empty());
}

TEST(SpotShadow, RejectsCapacityOverflow)
{
    std::array<LightData, kMaxSpotLightShadow + 1> lights;
    uint32_t                                       shadowIndex = 0;
    for (auto& light : lights)
    {
        light.lightType     = GpuLightType::Spot;
        light.shadowEnabled = 1;
        light.shadowIdx     = shadowIndex++;
        light.direction     = {0, 0, -1, 0};
    }
    EXPECT_DEATH(prepareShadows({}, lights, {}), "");
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
