#include "core/ConfigManager.h"
#include "core/Context.h"
#include "core/Logger.h"
#include "core/Window.h"
#include "render/device/FrameContext.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/Buffer.h"
#include "render/resources/GpuShader.h"
#include "render/resources/MaterialData.h"

#include <array>
#include <cmath>

namespace
{
constexpr size_t kCaseCount = 8;

void near(float actual, float expected)
{
    CHECK(
        std::isfinite(actual) && std::abs(actual - expected) < 0.0001f,
        "material result {} differs from {}",
        actual,
        expected
    );
}

void check(VulkanContext& vulkan, const char* shaderPath)
{
    std::array<MaterialData, kCaseCount> cases;
    for (auto& item : cases)
    {
        item.metallic    = 0.0f;
        item.roughness   = 0.5f;
        item.colorFactor = {0.4f, 0.5f, 0.8f, 1.0f};
    }
    cases[1].metallic                          = 1.0f;
    cases[1].colorFactor                       = {0.8f, 0.5f, 0.2f, 1.0f};
    cases[1].specularFactor                    = 0.3f;
    cases[2].metallic                          = 0.5f;
    cases[2].dielectricF0                      = 1.0f;
    cases[2].specularColorFactor               = {2.0f, 0.5f, 0.25f};
    cases[2].specularFactor                    = 0.5f;
    cases[2].specularTexture.textureIndex      = 1;
    cases[3].specularFactor                    = 0.0f;
    cases[4].workflow                          = 1;
    cases[4].specularColorFactor               = {0.4f, 0.5f, 0.6f};
    cases[4].specularColorTexture.textureIndex = 2;
    cases[4].glossinessFactor                  = 0.8f;
    cases[5].workflow                          = 1;
    cases[5].colorFactor                       = glm::vec4{0.0f};
    cases[5].specularColorFactor               = {0.8f, 0.5f, 0.2f};
    cases[5].glossinessFactor                  = 0.5f;
    cases[6].colorFactor                       = glm::vec4{0.0f};
    cases[6].specularFactor                    = 0.0f;
    cases[7].dielectricF0                      = 1.0f;

    auto&          device  = vulkan.deviceHandle();
    constexpr auto kMemory = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    Buffer
        input{vulkan.physicalDeviceHandle(), device, sizeof(cases), vk::BufferUsageFlagBits::eStorageBuffer, kMemory};
    std::array<std::array<glm::vec4, 5>, kCaseCount> values;
    Buffer
        output{vulkan.physicalDeviceHandle(), device, sizeof(values), vk::BufferUsageFlagBits::eStorageBuffer, kMemory};
    input.upload(cases.data(), sizeof(cases));

    const std::array bindings{
        vk::DescriptorSetLayoutBinding{
            .binding         = 0,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute
        },
        vk::DescriptorSetLayoutBinding{
            .binding         = 1,
            .descriptorType  = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags      = vk::ShaderStageFlagBits::eCompute
        },
    };
    auto layout =
        vkCheck(device.createDescriptorSetLayout({.bindingCount = bindings.size(), .pBindings = bindings.data()}));
    const auto                   rawLayout = *layout;
    const vk::DescriptorPoolSize poolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = 2};
    auto                         pool = vkCheck(device.createDescriptorPool({
                                .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                .maxSets       = 1,
                                .poolSizeCount = 1,
                                .pPoolSizes    = &poolSize,
    }));
    auto                         sets = vkCheck(device.allocateDescriptorSets({
                                .descriptorPool     = *pool,
                                .descriptorSetCount = 1,
                                .pSetLayouts        = &rawLayout,
    }));
    const std::array             bufferInfos{
        vk::DescriptorBufferInfo{.buffer = output.handle(), .range = output.size()},
        vk::DescriptorBufferInfo{.buffer = input.handle(), .range = input.size()},
    };
    for (uint32_t index = 0; index < bufferInfos.size(); ++index)
    {
        device.updateDescriptorSets(
            vk::WriteDescriptorSet{
                .dstSet          = *sets[0],
                .dstBinding      = index,
                .descriptorCount = 1,
                .descriptorType  = vk::DescriptorType::eStorageBuffer,
                .pBufferInfo     = &bufferInfos[index]
            },
            {}
        );
    }

    auto      pipelineLayout = vkCheck(device.createPipelineLayout({.setLayoutCount = 1, .pSetLayouts = &rawLayout}));
    GpuShader shader{device, shaderPath};
    auto      pipeline = vkCheck(device.createComputePipeline(
        vk::raii::PipelineCache{nullptr},
        {
                 .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = shader.handle(), .pName = "computeMain"},
                 .layout = *pipelineLayout,
        }
    ));

    FrameContext frame;
    frame.init(vulkan);
    auto& commands = frame.commandBufferHandle();
    vkCheck(commands.begin({}));
    commands.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commands.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, *sets[0], {});
    commands.dispatch(1, 1, 1);
    const vk::MemoryBarrier2 barrier{
        .srcStageMask  = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask  = vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eHostRead
    };
    commands.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &barrier});
    vkCheck(commands.end());
    vkCheck(device.resetFences(frame.drawFenceHandle()));
    const auto command = *commands;
    vkCheck(vulkan.queueHandle()
                .submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &command}, frame.drawFenceHandle()));
    frame.wait();
    output.download(values.data(), sizeof(values));
    frame.reset();

    const std::array<glm::vec4, kCaseCount> expectedDiffuse{
        glm::vec4{0.384f, 0.48f, 0.768f, 0.5f},
        {0, 0, 0, 0.5f},
        {0.16f, 0.2f, 0.32f, 0.5f},
        {0.4f, 0.5f, 0.8f, 0.5f},
        {0.256f, 0.32f, 0.512f, 0.6f},
        {0, 0, 0, 0.5f},
        {0, 0, 0, 0.5f},
        {0, 0, 0, 0.5f},
    };
    const std::array<glm::vec4, kCaseCount> expectedSpecular{
        glm::vec4{0.04f, 0.04f, 0.04f, 1},
        {0.8f, 0.5f, 0.2f, 1},
        {0.3f, 0.3f, 0.425f, 0.6f},
        {0, 0, 0, 0},
        {0.08f, 0.2f, 0.36f, 1},
        {0.8f, 0.5f, 0.2f, 1},
        {0, 0, 0, 0},
        {1, 1, 1, 1},
    };
    for (size_t index = 0; index < kCaseCount; ++index)
    {
        for (glm::length_t channel = 0; channel < 4; ++channel)
        {
            near(values[index][0][channel], expectedDiffuse[index][channel]);
            near(values[index][1][channel], expectedSpecular[index][channel]);
            CHECK(std::isfinite(values[index][2][channel]));
        }
        near(values[index][2].w, index == 0 ? 1.0f : index == 1 ? 0.288f / 0.988f : 0.0f);
        near(values[index][3].w, 0.0f);
        for (glm::length_t channel = 0; channel < 3; ++channel)
        {
            const float expected = values[index][4][channel];
            CHECK(
                std::isfinite(values[index][3][channel]) &&
                    std::abs(values[index][3][channel] - expected) < 0.08f * expected + 0.002f,
                "sample/PDF integral mismatch for case {}",
                index
            );
        }
        LOG_INFO("Material case {} passed", index);
    }
    for (size_t field = 0; field < 5; ++field)
        for (glm::length_t channel = 0; channel < 4; ++channel)
            if (field != 2 || channel != 3)
                near(values[1][field][channel], values[5][field][channel]);
}
}

int main(int argc, char** argv)
{
    logger::init(argv[0]);
    CHECK(argc == 2);
    ConfigManager config;
    context().config = &config;
    config.init();
    const auto original = config.editorConfig();
    config.setEditorSize(64, 64);
    CHECK(glfwInit());
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    Window window;
    window.init();
    VulkanContext vulkan;
    vulkan.init(window);
    check(vulkan, argv[1]);
    vulkan.reset();
    window.shutdown();
    config.setEditorSize(original.width, original.height);
    config.shutdown();
    context() = {};
}
