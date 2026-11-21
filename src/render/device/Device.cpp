#include "render/device/Device.h"

#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "core/Vertex.h"
#include "render/RenderContext.h"
#include "render/RenderConfig.h"
#include "render/device/VkCheck.h"
#include "render/device/VulkanContext.h"
#include "render/resources/Buffer.h"
#include "render/resources/MaterialData.h"
#include "render/resources/NativeFormats.h"
#include "render/resources/ShaderLayout.h"
#include "render/resources/Texture.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <utility>
#include <vulkan/vulkan_hash.hpp>

namespace
{
constexpr vk::VertexInputBindingDescription kVertexBinding{
    .binding   = 0,
    .stride    = sizeof(Vertex),
    .inputRate = vk::VertexInputRate::eVertex,
};

constexpr std::array kVertexAttributes{
    vk::VertexInputAttributeDescription{
        .location = 0,
        .binding  = 0,
        .format   = vk::Format::eR32G32B32Sfloat,
        .offset   = static_cast<uint32_t>(offsetof(Vertex, position)),
    },
    vk::VertexInputAttributeDescription{
        .location = 1,
        .binding  = 0,
        .format   = vk::Format::eR32G32B32Sfloat,
        .offset   = static_cast<uint32_t>(offsetof(Vertex, normal)),
    },
    vk::VertexInputAttributeDescription{
        .location = 2,
        .binding  = 0,
        .format   = vk::Format::eR32G32Sfloat,
        .offset   = static_cast<uint32_t>(offsetof(Vertex, texcoord)),
    },
    vk::VertexInputAttributeDescription{
        .location = 3,
        .binding  = 0,
        .format   = vk::Format::eR32G32B32A32Sfloat,
        .offset   = static_cast<uint32_t>(offsetof(Vertex, tangent)),
    },
};

void hashCombine(size_t& seed, size_t value)
{
    constexpr size_t kHashGoldenRatio = 0x9e3779b97f4a7c15ULL;
    seed ^= value + kHashGoldenRatio + (seed << 6) + (seed >> 2);
}

vk::ImageUsageFlags toVulkanImageUsage(Falcor::ResourceBindFlags flags)
{
    vk::ImageUsageFlags usage;
    if (is_set(flags, Falcor::ResourceBindFlags::ShaderResource))
        usage |= vk::ImageUsageFlagBits::eSampled;
    if (is_set(flags, Falcor::ResourceBindFlags::UnorderedAccess))
        usage |= vk::ImageUsageFlagBits::eStorage;
    if (is_set(flags, Falcor::ResourceBindFlags::RenderTarget))
        usage |= vk::ImageUsageFlagBits::eColorAttachment;
    if (is_set(flags, Falcor::ResourceBindFlags::DepthStencil))
        usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    return usage;
}

}  // namespace

vk::Extent3D ComputePipeline::groupCount(vk::Extent3D threads) const
{
    const auto roundUp = [](uint32_t count, uint32_t size) { return count / size + (count % size != 0); };
    return {
        roundUp(threads.width, threadGroupSize.width),
        roundUp(threads.height, threadGroupSize.height),
        roundUp(threads.depth, threadGroupSize.depth),
    };
}

void ComputePipeline::dispatch(vk::raii::CommandBuffer& commands, vk::Extent3D threads) const
{
    const auto groups = groupCount(threads);
    commands.dispatch(groups.width, groups.height, groups.depth);
}

namespace Falcor
{
Device::Device() : mpRenderContext(std::make_unique<RenderContext>(this)) {}

Device::~Device() { DCHECK(!initialized); }

void Device::wait()
{
    DCHECK(vulkan);
    vulkan->waitIdle();
}

void Device::init(VulkanContext& vulkan, ShaderManager& targetShaders)
{
    DCHECK(!initialized && !vulkan.mpDevice);
    this->vulkan = &vulkan;
    vulkan.mpDevice = this;
    device       = &vulkan.deviceHandle();
    shaders      = &targetShaders;
    mpRenderContext->vulkan        = &vulkan;
    mpRenderContext->shaderManager = &targetShaders;

    // Global material arrays plus shared/pass sets, with room for graph rebuilds.
    constexpr uint32_t kMaxDescriptorSets             = 128;
    constexpr uint32_t kSampledImageCapacity          = kMaxMaterialTextures * maxFramesInFlight + 512;
    constexpr uint32_t kSamplerCapacity               = 128;
    constexpr uint32_t kUniformBufferCapacity         = 32;
    constexpr uint32_t kStorageBufferCapacity         = 64;
    constexpr uint32_t kStorageImageCapacity          = 32;
    constexpr uint32_t kAccelerationStructureCapacity = 8;
    std::vector        poolSizes{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampledImage, .descriptorCount = kSampledImageCapacity},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eSampler, .descriptorCount = kSamplerCapacity},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer, .descriptorCount = kUniformBufferCapacity},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageBuffer, .descriptorCount = kStorageBufferCapacity},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eStorageImage, .descriptorCount = kStorageImageCapacity},
    };
    if (vulkan.supportsRayTracing())
        poolSizes.push_back({
            .type            = vk::DescriptorType::eAccelerationStructureKHR,
            .descriptorCount = kAccelerationStructureCapacity,
        });
    descriptorPool = vkCheck(device->createDescriptorPool({
        .flags         = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets       = kMaxDescriptorSets,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes    = poolSizes.data(),
    }));
    pipelineCache  = vkCheck(device->createPipelineCache(vk::PipelineCacheCreateInfo{}));
    createSharedLayouts();
    DEBUG_EXEC(initialized = true);
}

void Device::shutdown() noexcept
{
    DCHECK(initialized);
    *mpRenderContext = RenderContext(this);
    pipelines.clear();
    editorPipelineLayout = nullptr;
    meshPipelineLayout   = nullptr;
    pipelineLayouts.clear();
    editorViewDescriptorSetLayout = {};
    materialDescriptorSetLayout   = {};
    lightDescriptorSetLayout      = {};
    shadowDescriptorSetLayout     = {};
    sceneDescriptorSetLayout      = {};
    pipelineCache                 = nullptr;
    descriptorPool                = nullptr;
    shaders                       = nullptr;
    device                        = nullptr;
    vulkan->mpDevice              = nullptr;
    vulkan                        = nullptr;
    DEBUG_EXEC(initialized = false);
}

Falcor::ResourceBindFlags Device::getFormatBindFlags(Falcor::ResourceFormat format) const
{
    const auto features =
        vulkan->physicalDeviceHandle().getFormatProperties(Falcor::getVulkanFormat(format)).optimalTilingFeatures;
    auto flags = Falcor::ResourceBindFlags::None;
    if (features & vk::FormatFeatureFlagBits::eSampledImage)
        flags = flags | Falcor::ResourceBindFlags::ShaderResource;
    if (features & vk::FormatFeatureFlagBits::eStorageImage)
        flags = flags | Falcor::ResourceBindFlags::UnorderedAccess;
    if (features & vk::FormatFeatureFlagBits::eColorAttachment)
        flags = flags | Falcor::ResourceBindFlags::RenderTarget;
    if (features & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
        flags = flags | Falcor::ResourceBindFlags::DepthStencil;
    return flags;
}

ref<Buffer> Device::createBuffer(size_t size, ResourceBindFlags bindFlags, MemoryType memoryType, const void* initData)
{
    CHECK(!is_set(bindFlags, ResourceBindFlags::Shared), "shared buffers require external memory");
    const auto limit = vulkan->properties().limits.maxStorageBufferRange;
    CHECK(
        !is_set(bindFlags, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess) || size <= limit,
        "storage buffer requires {} bytes; maxStorageBufferRange is {}",
        size,
        limit
    );
    return make_ref<Buffer>(ref<Device>(this), size, bindFlags, memoryType, initData);
}

ref<Texture> Device::createTexture(
    Resource::Type    type,
    Falcor::ResourceFormat    format,
    vk::Extent3D      extent,
    uint32_t          arraySize,
    uint32_t          mipLevels,
    uint32_t          samples,
    Falcor::ResourceBindFlags bindFlags
) const
{
    CHECK(!is_set(bindFlags, Falcor::ResourceBindFlags::Shared), "shared textures require external memory");
    const bool     cube      = type == Resource::Type::TextureCube;
    const auto     imageType = type == Resource::Type::Texture1D ? vk::ImageType::e1D
            : type == Resource::Type::Texture3D                  ? vk::ImageType::e3D
                                                                 : vk::ImageType::e2D;
    constexpr auto kTransfer = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    return make_ref<Texture>(
        *vulkan,
        vk::ImageCreateInfo{
            .flags         = cube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags{},
            .imageType     = imageType,
            .format        = Falcor::getVulkanFormat(format),
            .extent        = extent,
            .mipLevels     = mipLevels,
            .arrayLayers   = cube ? arraySize * 6 : arraySize,
            .samples       = static_cast<vk::SampleCountFlagBits>(samples),
            .tiling        = vk::ImageTiling::eOptimal,
            .usage         = toVulkanImageUsage(bindFlags) | kTransfer,
            .sharingMode   = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        },
        bindFlags
    );
}

ref<Texture> Device::createTexture1D(
    uint32_t width, Falcor::ResourceFormat format, uint32_t arraySize, uint32_t mipLevels, Falcor::ResourceBindFlags bindFlags
) const
{
    return createTexture(Resource::Type::Texture1D, format, {width, 1, 1}, arraySize, mipLevels, 1, bindFlags);
}

ref<Texture> Device::createTexture2D(
    uint32_t          width,
    uint32_t          height,
    Falcor::ResourceFormat    format,
    uint32_t          arraySize,
    uint32_t          mipLevels,
    Falcor::ResourceBindFlags bindFlags
) const
{
    return createTexture(Resource::Type::Texture2D, format, {width, height, 1}, arraySize, mipLevels, 1, bindFlags);
}

ref<Texture> Device::createTexture2DMS(
    uint32_t          width,
    uint32_t          height,
    Falcor::ResourceFormat    format,
    uint32_t          sampleCount,
    uint32_t          arraySize,
    Falcor::ResourceBindFlags bindFlags
) const
{
    CHECK(sampleCount > 1 && std::has_single_bit(sampleCount) && sampleCount <= 64);
    return createTexture(
        Resource::Type::Texture2DMultisample,
        format,
        {width, height, 1},
        arraySize,
        1,
        sampleCount,
        bindFlags
    );
}

ref<Texture> Device::createTexture3D(
    uint32_t          width,
    uint32_t          height,
    uint32_t          depth,
    Falcor::ResourceFormat    format,
    uint32_t          mipLevels,
    Falcor::ResourceBindFlags bindFlags
) const
{
    return createTexture(Resource::Type::Texture3D, format, {width, height, depth}, 1, mipLevels, 1, bindFlags);
}

ref<Texture> Device::createTextureCube(
    uint32_t          width,
    uint32_t          height,
    Falcor::ResourceFormat    format,
    uint32_t          arraySize,
    uint32_t          mipLevels,
    Falcor::ResourceBindFlags bindFlags
) const
{
    return createTexture(Resource::Type::TextureCube, format, {width, height, 1}, arraySize, mipLevels, 1, bindFlags);
}

DescriptorSetLayout Device::createDescriptorSetLayout(std::span<const vk::DescriptorSetLayoutBinding> bindings) const
{
    DCHECK(device);
    DescriptorSetLayout result;
    result.bindings.assign(bindings.begin(), bindings.end());
    result.handle = vkCheck(device->createDescriptorSetLayout({
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings    = bindings.data(),
    }));
    return result;
}

vk::PipelineLayout Device::getOrCreatePipelineLayout(
    std::span<const DescriptorSetLayout* const> sets, std::span<const vk::PushConstantRange> ranges
)
{
    PipelineLayoutKey key;
    key.ranges.assign(ranges.begin(), ranges.end());
    for (const auto* set : sets)
    {
        auto& bindings = key.sets.emplace_back(set->bindings);
        std::ranges::sort(bindings, {}, &vk::DescriptorSetLayoutBinding::binding);
    }
    if (const auto found = pipelineLayouts.find(key); found != pipelineLayouts.end())
        return *found->second.handle;

    // Cached layouts can create pipelines after the pass that supplied a set layout has been rebuilt.
    CachedPipelineLayout                 cached;
    std::vector<vk::DescriptorSetLayout> handles;
    for (const auto& bindings : key.sets)
    {
        auto& layout = cached.setLayouts.emplace_back(vkCheck(device->createDescriptorSetLayout({
            .bindingCount = static_cast<uint32_t>(bindings.size()),
            .pBindings    = bindings.data(),
        })));
        handles.push_back(*layout);
    }
    cached.handle                   = vkCheck(device->createPipelineLayout({
                          .setLayoutCount         = static_cast<uint32_t>(handles.size()),
                          .pSetLayouts            = handles.data(),
                          .pushConstantRangeCount = static_cast<uint32_t>(ranges.size()),
                          .pPushConstantRanges    = ranges.data(),
    }));
    const vk::PipelineLayout handle = *cached.handle;
    pipelineLayouts.emplace(std::move(key), std::move(cached));
    return handle;
}

size_t Device::PipelineLayoutKeyHash::operator()(const PipelineLayoutKey& key) const noexcept
{
    size_t seed = 0;
    hashCombine(seed, key.sets.size());
    for (const auto& set : key.sets)
    {
        hashCombine(seed, set.size());
        for (const auto& binding : set)
            hashCombine(seed, std::hash<vk::DescriptorSetLayoutBinding>{}(binding));
    }
    hashCombine(seed, key.ranges.size());
    for (const auto& range : key.ranges)
        hashCombine(seed, std::hash<vk::PushConstantRange>{}(range));
    return seed;
}

vk::raii::DescriptorSet Device::allocateDescriptorSet(const DescriptorSetLayout& layout) const
{
    DCHECK(device && *descriptorPool);
    const auto handle = *layout;
    return std::move(vkCheck(device->allocateDescriptorSets({
                                 .descriptorPool     = *descriptorPool,
                                 .descriptorSetCount = 1,
                                 .pSetLayouts        = &handle,
                             }))
                         .front());
}

ComputePipeline Device::createComputePipeline(ShaderHandle shader, vk::PipelineLayout layout) const
{
    DCHECK(device && shaders);
    const auto& metadata = shaders->metadata(shader);
    CHECK(metadata.entries.size() == 1, "compute shader '{}' must have one entry point", metadata.binary.string());
    const auto& entry           = metadata.entries.front();
    const auto  threadGroupSize = shaders->layout(shader).threadGroupSize(entry);
    return {
        .handle          = vkCheck(device->createComputePipeline(
            nullptr,
            {
                         .stage =
                    {
                                 .stage  = vk::ShaderStageFlagBits::eCompute,
                                 .module = shaders->module(shader),
                                 .pName  = entry.c_str(),
                    },
                         .layout = layout,
            }
        )),
        .threadGroupSize = threadGroupSize,
    };
}

std::size_t Device::PipelineKeyHash::operator()(const PipelineKey& key) const noexcept
{
    std::size_t seed    = 0;
    const auto  combine = [&seed](uint64_t value) { hashCombine(seed, std::hash<uint64_t>{}(value)); };
    combine(key.shader.index);
    hashCombine(seed, std::hash<vk::PipelineLayout>{}(key.layout));
    combine(static_cast<uint32_t>(key.state.vertexLayout));
    combine(static_cast<uint32_t>(key.state.topology));
    combine(static_cast<uint32_t>(key.state.polygonMode));
    combine(static_cast<uint32_t>(key.state.cullMode));
    combine(static_cast<uint32_t>(key.state.frontFace));
    combine(static_cast<uint32_t>(key.state.samples));
    combine(static_cast<uint32_t>(key.state.depthCompare));
    combine(key.state.depthClamp);
    combine(key.state.depthTest);
    combine(key.state.depthWrite);
    combine(key.state.blend);
    combine(key.colorFormats.size());
    for (const auto format : key.colorFormats)
        combine(static_cast<uint32_t>(format));
    combine(static_cast<uint32_t>(key.depthFormat));
    combine(key.viewMask);
    return seed;
}

vk::Pipeline Device::getOrCreatePipeline(const GraphicsPipelineInfo& info)
{
    const PipelineKey key{
        .shader       = info.shader,
        .layout       = info.layout,
        .state        = info.state,
        .colorFormats = info.colorFormats,
        .depthFormat  = info.depthFormat,
        .viewMask     = info.viewMask,
    };
    if (const auto found = pipelines.find(key); found != pipelines.end())
        return *found->second;

    const ShaderMetadata& metadata = shaders->metadata(info.shader);
    CHECK(metadata.entries.size() == 2);
    const std::array shaderStages = {
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eVertex,
            .module = shaders->module(key.shader),
            .pName  = metadata.entries[0].c_str()
        },
        vk::PipelineShaderStageCreateInfo{
            .stage  = vk::ShaderStageFlagBits::eFragment,
            .module = shaders->module(key.shader),
            .pName  = metadata.entries[1].c_str()
        },
    };
    constexpr std::array kPositionUvAttributes{kVertexAttributes[0], kVertexAttributes[2]};
    std::span<const vk::VertexInputAttributeDescription> attributes;
    switch (key.state.vertexLayout)
    {
        case VertexLayoutPreset::Mesh:
            attributes = kVertexAttributes;
            break;
        case VertexLayoutPreset::PositionOnly:
            attributes = std::span{kVertexAttributes}.first<1>();
            break;
        case VertexLayoutPreset::PositionUv:
            attributes = kPositionUvAttributes;
            break;
        case VertexLayoutPreset::None:
            break;
    }
    const bool                                   hasVertexInput = !attributes.empty();
    const vk::PipelineVertexInputStateCreateInfo vertexInput{
        .vertexBindingDescriptionCount   = hasVertexInput ? 1u : 0u,
        .pVertexBindingDescriptions      = hasVertexInput ? &kVertexBinding : nullptr,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size()),
        .pVertexAttributeDescriptions    = attributes.data(),
    };
    const vk::PipelineInputAssemblyStateCreateInfo inputAssembly{.topology = key.state.topology};
    constexpr vk::PipelineViewportStateCreateInfo  viewport{.viewportCount = 1, .scissorCount = 1};
    const vk::PipelineRasterizationStateCreateInfo raster{
        .depthClampEnable = key.state.depthClamp,
        .polygonMode      = key.state.polygonMode,
        .cullMode         = key.state.cullMode,
        .frontFace        = key.state.frontFace,
        .lineWidth        = 1.0f,
    };
    const vk::PipelineMultisampleStateCreateInfo multisample{
        .rasterizationSamples = key.state.samples,
    };
    const vk::PipelineDepthStencilStateCreateInfo depth{
        .depthTestEnable  = key.state.depthTest,
        .depthWriteEnable = key.state.depthWrite,
        .depthCompareOp   = key.state.depthCompare,
    };
    const vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable         = key.state.blend,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp        = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .alphaBlendOp        = vk::BlendOp::eAdd,
        .colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };
    const std::vector                           colorBlendAttachments(key.colorFormats.size(), colorBlendAttachment);
    const vk::PipelineColorBlendStateCreateInfo colorBlend{
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
        .pAttachments    = colorBlendAttachments.data(),
    };
    constexpr std::array                     dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    const vk::PipelineDynamicStateCreateInfo dynamic{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data(),
    };
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo> chain = {
        {.stageCount          = static_cast<uint32_t>(shaderStages.size()),
         .pStages             = shaderStages.data(),
         .pVertexInputState   = &vertexInput,
         .pInputAssemblyState = &inputAssembly,
         .pViewportState      = &viewport,
         .pRasterizationState = &raster,
         .pMultisampleState   = &multisample,
         .pDepthStencilState  = key.depthFormat == vk::Format::eUndefined ? nullptr : &depth,
         .pColorBlendState    = &colorBlend,
         .pDynamicState       = &dynamic,
         .layout              = key.layout},
        {.viewMask                = key.viewMask,
         .colorAttachmentCount    = static_cast<uint32_t>(key.colorFormats.size()),
         .pColorAttachmentFormats = key.colorFormats.data(),
         .depthAttachmentFormat   = key.depthFormat},
    };
    auto pipeline = vkCheck(device->createGraphicsPipeline(pipelineCache, chain.get<vk::GraphicsPipelineCreateInfo>()));
    const vk::Pipeline handle = *pipeline;
    pipelines.emplace(key, std::move(pipeline));
    return handle;
}

FALCOR_SCRIPT_BINDING(Device)
{
    using namespace ScriptBindings;
    FALCOR_SCRIPT_BINDING_DEPENDENCY(Formats)

    static PyMethodDef kMethods[]{
        {"create_buffer", reinterpret_cast<PyCFunction>(+[](PyObject* object, PyObject* args, PyObject* keywords) -> PyObject*
        {
            static const char* const kKeywords[]{"size", "bind_flags", "memory_type", nullptr};
            PyObject* sizeArg = nullptr;
            PyObject* bindFlagsArg = nullptr;
            PyObject* memoryTypeArg = nullptr;
            if (!PyArg_ParseTupleAndKeywords(args, keywords, "O|OO:create_buffer", const_cast<char**>(kKeywords),
                                            &sizeArg, &bindFlagsArg, &memoryTypeArg))
                return nullptr;

            const auto size = fromPython<size_t>(sizeArg);
            const auto bindFlags = bindFlagsArg ? fromPython<ResourceBindFlags>(bindFlagsArg)
                                               : std::optional{ResourceBindFlags::None};
            const auto memoryType = memoryTypeArg ? fromPython<MemoryType>(memoryTypeArg)
                                                : std::optional{MemoryType::DeviceLocal};
            if (PyErr_Occurred())
                return nullptr;
            if (!size || !bindFlags || !memoryType)
            {
                PyErr_SetString(PyExc_TypeError, "create_buffer requires an integer size, ResourceBindFlags and MemoryType");
                return nullptr;
            }
            if (!*size)
            {
                PyErr_SetString(PyExc_ValueError, "buffer size must be greater than zero");
                return nullptr;
            }

            const auto& device = valueOf<ref<Device>>(object);
            if (!device->getRenderContext()->vulkan)
            {
                PyErr_SetString(PyExc_RuntimeError, "device is not initialized");
                return nullptr;
            }
            return toPython(ref<Resource>(device->createBuffer(*size, *bindFlags, *memoryType))).release();
        }), METH_VARARGS | METH_KEYWORDS},
        {"wait", [](PyObject* object, PyObject*) -> PyObject*
        {
            const auto& device = valueOf<ref<Device>>(object);
            if (!device->getRenderContext()->vulkan)
            {
                PyErr_SetString(PyExc_RuntimeError, "device is not initialized");
                return nullptr;
            }
            device->wait();
            Py_RETURN_NONE;
        }, METH_NOARGS},
        {},
    };
    static PyType_Slot kSlots[]{
        {Py_tp_dealloc, reinterpret_cast<void*>(destroyPythonValue<ref<Device>>)},
        {Py_tp_methods, kMethods},
        {},
    };
    static PyType_Spec kType{
        "falcor.Device", sizeof(PythonValue<ref<Device>>), 0,
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION, kSlots,
    };

    const auto device = makePythonType<ref<Device>>(m, kType);
    return device ? PyModule_AddObjectRef(m, "Device", device.get()) : -1;
}
}  // namespace Falcor
