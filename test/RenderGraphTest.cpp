#include "render/pass/RenderGraph.h"

#include <gtest/gtest.h>

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
