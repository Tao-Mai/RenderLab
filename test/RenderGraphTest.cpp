#include "render/feature/RenderGraph.h"
#include "render/PipelineManager.h"

#include <glm/gtc/matrix_transform.hpp>

#include <gtest/gtest.h>

TEST(PipelineWinding, PreservesCcwFrontFacesWithVulkanProjection)
{
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    projection[1][1] *= -1.0f;
    const glm::vec3 vertices[] = {{-1.0f, -1.0f, -3.0f}, {1.0f, -1.0f, -3.0f}, {1.0f, 1.0f, -3.0f}};
    glm::vec2 projected[3];
    for (size_t index = 0; index < 3; ++index)
    {
        const glm::vec4 clip = projection * glm::vec4(vertices[index], 1.0f);
        projected[index] = glm::vec2(clip) / clip.w;
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
