#include "render/pass/RenderGraph.h"

#include <algorithm>
#include <string_view>

#include <gtest/gtest.h>

namespace
{
using Desc = RenderGraph::ResourceDesc;
using State = RenderGraph::ResourceState;
using Type = RenderGraph::ResourceType;

Desc colorTarget()
{
    return {.format = vk::Format::eR8G8B8A8Unorm,
        .extent = {64, 64, 1},
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .stages = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        .access = vk::AccessFlagBits2::eColorAttachmentWrite,
        .layout = vk::ImageLayout::eColorAttachmentOptimal};
}

Desc sampledImage()
{
    return {.imageUsage = vk::ImageUsageFlagBits::eSampled,
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eShaderReadOnlyOptimal};
}

bool contains(const std::vector<std::string>& errors, std::string_view expected)
{
    return std::ranges::any_of(errors, [&](const auto& error) { return error.find(expected) != error.npos; });
}
}

TEST(RenderGraph, ResolvesSlotChainMergesUsageAndSortsDependencies)
{
    RenderGraph graph;
    auto [consumerInputs, consumerOutputs] = graph.registerPass("consumer", 2, 1);
    auto [producerInputs, producerOutputs] = graph.registerPass("producer", 1, 2);
    ASSERT_EQ(consumerInputs.size(), 2);
    ASSERT_EQ(consumerOutputs.size(), 1);
    ASSERT_EQ(producerOutputs.size(), 2);

    graph.createResource(producerInputs[0], colorTarget());
    graph.bindOutput(producerOutputs[0], producerInputs[0], colorTarget());
    graph.bindOutput(producerOutputs[1], producerInputs[0], colorTarget());
    graph.bindInput(consumerInputs[0], producerOutputs[0], sampledImage());
    graph.ignoreInput(consumerInputs[1]);
    graph.bindOutput(consumerOutputs[0], consumerInputs[0], sampledImage());

    EXPECT_TRUE(graph.compile().empty());
    EXPECT_EQ(graph.executionOrder(), (std::vector<std::string>{"producer", "consumer"}));
    EXPECT_EQ(&graph.resourceDesc(producerInputs[0]), &graph.resourceDesc(consumerInputs[0]));
    EXPECT_EQ(graph.resourceDesc(producerInputs[0]).imageUsage,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
}

TEST(RenderGraph, RejectsUnboundAndIgnoredOutputResources)
{
    RenderGraph graph;
    auto [inputs, outputs] = graph.registerPass("pass", 2, 1);
    graph.ignoreInput(inputs[0]);
    graph.bindOutput(outputs[0], inputs[0], sampledImage());

    const auto errors = graph.compile();
    EXPECT_TRUE(contains(errors, "unbound input"));
    EXPECT_TRUE(contains(errors, "has no resource"));
}

TEST(RenderGraph, RejectsReferenceAndPassCycles)
{
    RenderGraph graph;
    auto [aInputs, aOutputs] = graph.registerPass("a", 1, 1);
    auto [bInputs, bOutputs] = graph.registerPass("b", 1, 1);
    graph.bindOutput(aOutputs[0], aInputs[0], sampledImage());
    graph.bindOutput(bOutputs[0], bInputs[0], sampledImage());
    graph.bindInput(aInputs[0], bOutputs[0], sampledImage());
    graph.bindInput(bInputs[0], aOutputs[0], sampledImage());

    const auto errors = graph.compile();
    EXPECT_TRUE(contains(errors, "resource reference cycle"));
    EXPECT_TRUE(contains(errors, "pass dependency cycle"));
    EXPECT_TRUE(graph.executionOrder().empty());
}

TEST(RenderGraph, DisabledProducerCannotSatisfyEnabledConsumer)
{
    RenderGraph graph;
    auto [aInputs, aOutputs] = graph.registerPass("producer", 1, 1);
    auto [bInputs, bOutputs] = graph.registerPass("consumer", 1, 0);
    graph.setPassEnabled("producer", false);
    graph.bindInput(bInputs[0], aOutputs[0], sampledImage());

    EXPECT_TRUE(contains(graph.compile(), "disabled pass"));
    graph.setPassEnabled("consumer", false);
    EXPECT_TRUE(graph.compile().empty());
    EXPECT_TRUE(graph.executionOrder().empty());
}

TEST(RenderGraph, RejectsUnorderedWritesToAliasedResource)
{
    RenderGraph graph;
    auto [aInputs, aOutputs] = graph.registerPass("producer", 1, 1);
    auto [bInputs, bOutputs] = graph.registerPass("reader", 1, 0);
    auto [cInputs, cOutputs] = graph.registerPass("writer", 1, 0);
    graph.createResource(aInputs[0], colorTarget());
    graph.bindOutput(aOutputs[0], aInputs[0], colorTarget());
    graph.bindInput(bInputs[0], aOutputs[0], sampledImage());
    graph.bindInput(cInputs[0], aOutputs[0], colorTarget());

    EXPECT_TRUE(contains(graph.compile(), "unordered shared-resource hazard"));
}

TEST(RenderGraph, AllowsUnorderedReaders)
{
    RenderGraph graph;
    auto [aInputs, aOutputs] = graph.registerPass("producer", 1, 1);
    auto [bInputs, bOutputs] = graph.registerPass("reader1", 1, 0);
    auto [cInputs, cOutputs] = graph.registerPass("reader2", 1, 0);
    graph.createResource(aInputs[0], colorTarget());
    graph.bindOutput(aOutputs[0], aInputs[0], colorTarget());
    graph.bindInput(bInputs[0], aOutputs[0], sampledImage());
    graph.bindInput(cInputs[0], aOutputs[0], sampledImage());

    EXPECT_TRUE(graph.compile().empty());
}

TEST(RenderGraph, ValidatesTypeAndCreationRequirements)
{
    RenderGraph graph;
    auto [aInputs, aOutputs] = graph.registerPass("producer", 1, 1);
    auto [bInputs, bOutputs] = graph.registerPass("consumer", 1, 0);
    graph.createResource(aInputs[0], {.type = Type::Buffer});
    graph.bindOutput(aOutputs[0], aInputs[0], {.type = Type::Buffer});
    graph.bindInput(bInputs[0], aOutputs[0], sampledImage());

    const auto errors = graph.compile();
    EXPECT_TRUE(contains(errors, "resource type mismatch"));
    EXPECT_TRUE(contains(errors, "incomplete buffer creation"));
}

TEST(RenderGraph, ExternalUsageCannotBeExpandedByConsumer)
{
    RenderGraph graph;
    auto [aInputs, aOutputs] = graph.registerPass("import", 1, 1);
    auto [bInputs, bOutputs] = graph.registerPass("consumer", 1, 0);
    const auto handle = vk::Image(reinterpret_cast<VkImage>(static_cast<uintptr_t>(1)));
    graph.importResource(aInputs[0], colorTarget(), {{.image = handle}});
    graph.bindOutput(aOutputs[0], aInputs[0], colorTarget());
    graph.bindInput(bInputs[0], aOutputs[0], sampledImage());

    EXPECT_TRUE(contains(graph.compile(), "lacks required usage flags"));
}

TEST(RenderGraph, ReimportedExternalResourceStillDetectsWriteHazards)
{
    RenderGraph graph;
    auto [aInputs, aOutputs] = graph.registerPass("a", 1, 0);
    auto [bInputs, bOutputs] = graph.registerPass("b", 1, 0);
    const auto handle = vk::Image(reinterpret_cast<VkImage>(static_cast<uintptr_t>(1)));
    graph.importResource(aInputs[0], colorTarget(), {{.image = handle}});
    graph.importResource(bInputs[0], colorTarget(), {{.image = handle}});

    EXPECT_TRUE(contains(graph.compile(), "unordered shared-resource hazard"));
}

TEST(RenderGraph, RejectsConflictingSimultaneousInputStates)
{
    RenderGraph graph;
    auto [inputs, outputs] = graph.registerPass("pass", 2, 0);
    const auto handle = vk::Image(reinterpret_cast<VkImage>(static_cast<uintptr_t>(1)));
    Desc desc = colorTarget();
    desc.imageUsage |= vk::ImageUsageFlagBits::eSampled;
    graph.importResource(inputs[0], desc, {{.image = handle}});
    desc.access = vk::AccessFlagBits2::eShaderSampledRead;
    desc.stages = vk::PipelineStageFlagBits2::eFragmentShader;
    desc.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    graph.importResource(inputs[1], desc, {{.image = handle}});

    EXPECT_TRUE(contains(graph.compile(), "conflicting input uses"));
}

TEST(RenderGraphBarrier, CoversDepthSamplingAndReadbackHazards)
{
    const State depthWrite{.stages = vk::PipelineStageFlagBits2::eLateFragmentTests,
        .access = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .layout = vk::ImageLayout::eDepthAttachmentOptimal};
    const Desc depthRead{.imageUsage = vk::ImageUsageFlagBits::eSampled,
        .stages = vk::PipelineStageFlagBits2::eFragmentShader,
        .access = vk::AccessFlagBits2::eShaderSampledRead,
        .layout = vk::ImageLayout::eDepthReadOnlyOptimal};
    EXPECT_TRUE(RenderGraph::needsBarrier(depthWrite, depthRead));
    EXPECT_FALSE(RenderGraph::needsBarrier({.stages = depthRead.stages,
        .access = depthRead.access, .layout = depthRead.layout}, depthRead));
    EXPECT_TRUE(RenderGraph::needsBarrier({}, colorTarget()));
    EXPECT_TRUE(RenderGraph::needsBarrier({.stages = vk::PipelineStageFlagBits2::eTransfer,
        .access = vk::AccessFlagBits2::eTransferWrite},
        {.type = Type::Buffer, .stages = vk::PipelineStageFlagBits2::eHost,
            .access = vk::AccessFlagBits2::eHostRead}));
    EXPECT_TRUE(RenderGraph::needsBarrier({.stages = vk::PipelineStageFlagBits2::eHost,
        .access = vk::AccessFlagBits2::eHostRead},
        {.type = Type::Buffer, .stages = vk::PipelineStageFlagBits2::eTransfer,
            .access = vk::AccessFlagBits2::eTransferWrite}));
}
