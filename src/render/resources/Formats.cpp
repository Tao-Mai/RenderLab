/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "render/resources/NativeFormats.h"

#include "core/Logger.h"
#include "core/ScriptBindings.h"
#include "render/resources/Buffer.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <string_view>
#include <utility>

namespace Falcor
{
const FormatDesc kFormatDesc[]{
    // clang-format off
    {ResourceFormat::Unknown,         "Unknown",          0,  0, FormatType::Unknown,   false, false, false, {1, 1}, {0, 0, 0, 0}},
    {ResourceFormat::R8Unorm,         "R8Unorm",          1,  1, FormatType::Unorm,     false, false, false, {1, 1}, {8, 0, 0, 0}},
    {ResourceFormat::R8Snorm,         "R8Snorm",          1,  1, FormatType::Snorm,     false, false, false, {1, 1}, {8, 0, 0, 0}},
    {ResourceFormat::R16Unorm,        "R16Unorm",         2,  1, FormatType::Unorm,     false, false, false, {1, 1}, {16, 0, 0, 0}},
    {ResourceFormat::R16Snorm,        "R16Snorm",         2,  1, FormatType::Snorm,     false, false, false, {1, 1}, {16, 0, 0, 0}},
    {ResourceFormat::RG8Unorm,        "RG8Unorm",         2,  2, FormatType::Unorm,     false, false, false, {1, 1}, {8, 8, 0, 0}},
    {ResourceFormat::RG8Snorm,        "RG8Snorm",         2,  2, FormatType::Snorm,     false, false, false, {1, 1}, {8, 8, 0, 0}},
    {ResourceFormat::RG16Unorm,       "RG16Unorm",        4,  2, FormatType::Unorm,     false, false, false, {1, 1}, {16, 16, 0, 0}},
    {ResourceFormat::RG16Snorm,       "RG16Snorm",        4,  2, FormatType::Snorm,     false, false, false, {1, 1}, {16, 16, 0, 0}},
    {ResourceFormat::RGB5A1Unorm,     "RGB5A1Unorm",      2,  4, FormatType::Unorm,     false, false, false, {1, 1}, {5, 5, 5, 1}},
    {ResourceFormat::RGBA8Unorm,      "RGBA8Unorm",       4,  4, FormatType::Unorm,     false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::RGBA8Snorm,      "RGBA8Snorm",       4,  4, FormatType::Snorm,     false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::RGB10A2Unorm,    "RGB10A2Unorm",     4,  4, FormatType::Unorm,     false, false, false, {1, 1}, {10, 10, 10, 2}},
    {ResourceFormat::RGB10A2Uint,     "RGB10A2Uint",      4,  4, FormatType::Uint,      false, false, false, {1, 1}, {10, 10, 10, 2}},
    {ResourceFormat::RGBA16Unorm,     "RGBA16Unorm",      8,  4, FormatType::Unorm,     false, false, false, {1, 1}, {16, 16, 16, 16}},
    {ResourceFormat::RGBA16Snorm,     "RGBA16Snorm",      8,  4, FormatType::Snorm,     false, false, false, {1, 1}, {16, 16, 16, 16}},
    {ResourceFormat::RGBA8UnormSrgb,  "RGBA8UnormSrgb",   4,  4, FormatType::UnormSrgb, false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::R16Float,        "R16Float",         2,  1, FormatType::Float,     false, false, false, {1, 1}, {16, 0, 0, 0}},
    {ResourceFormat::RG16Float,       "RG16Float",        4,  2, FormatType::Float,     false, false, false, {1, 1}, {16, 16, 0, 0}},
    {ResourceFormat::RGBA16Float,     "RGBA16Float",      8,  4, FormatType::Float,     false, false, false, {1, 1}, {16, 16, 16, 16}},
    {ResourceFormat::R32Float,        "R32Float",         4,  1, FormatType::Float,     false, false, false, {1, 1}, {32, 0, 0, 0}},
    {ResourceFormat::RG32Float,       "RG32Float",        8,  2, FormatType::Float,     false, false, false, {1, 1}, {32, 32, 0, 0}},
    {ResourceFormat::RGB32Float,      "RGB32Float",       12, 3, FormatType::Float,     false, false, false, {1, 1}, {32, 32, 32, 0}},
    {ResourceFormat::RGBA32Float,     "RGBA32Float",      16, 4, FormatType::Float,     false, false, false, {1, 1}, {32, 32, 32, 32}},
    {ResourceFormat::R11G11B10Float,  "R11G11B10Float",   4,  3, FormatType::Float,     false, false, false, {1, 1}, {11, 11, 10, 0}},
    {ResourceFormat::RGB9E5Float,     "RGB9E5Float",      4,  3, FormatType::Float,     false, false, false, {1, 1}, {9, 9, 9, 5}},
    {ResourceFormat::R8Int,           "R8Int",            1,  1, FormatType::Sint,      false, false, false, {1, 1}, {8, 0, 0, 0}},
    {ResourceFormat::R8Uint,          "R8Uint",           1,  1, FormatType::Uint,      false, false, false, {1, 1}, {8, 0, 0, 0}},
    {ResourceFormat::R16Int,          "R16Int",           2,  1, FormatType::Sint,      false, false, false, {1, 1}, {16, 0, 0, 0}},
    {ResourceFormat::R16Uint,         "R16Uint",          2,  1, FormatType::Uint,      false, false, false, {1, 1}, {16, 0, 0, 0}},
    {ResourceFormat::R32Int,          "R32Int",           4,  1, FormatType::Sint,      false, false, false, {1, 1}, {32, 0, 0, 0}},
    {ResourceFormat::R32Uint,         "R32Uint",          4,  1, FormatType::Uint,      false, false, false, {1, 1}, {32, 0, 0, 0}},
    {ResourceFormat::RG8Int,          "RG8Int",           2,  2, FormatType::Sint,      false, false, false, {1, 1}, {8, 8, 0, 0}},
    {ResourceFormat::RG8Uint,         "RG8Uint",          2,  2, FormatType::Uint,      false, false, false, {1, 1}, {8, 8, 0, 0}},
    {ResourceFormat::RG16Int,         "RG16Int",          4,  2, FormatType::Sint,      false, false, false, {1, 1}, {16, 16, 0, 0}},
    {ResourceFormat::RG16Uint,        "RG16Uint",         4,  2, FormatType::Uint,      false, false, false, {1, 1}, {16, 16, 0, 0}},
    {ResourceFormat::RG32Int,         "RG32Int",          8,  2, FormatType::Sint,      false, false, false, {1, 1}, {32, 32, 0, 0}},
    {ResourceFormat::RG32Uint,        "RG32Uint",         8,  2, FormatType::Uint,      false, false, false, {1, 1}, {32, 32, 0, 0}},
    {ResourceFormat::RGB32Int,        "RGB32Int",         12, 3, FormatType::Sint,      false, false, false, {1, 1}, {32, 32, 32, 0}},
    {ResourceFormat::RGB32Uint,       "RGB32Uint",        12, 3, FormatType::Uint,      false, false, false, {1, 1}, {32, 32, 32, 0}},
    {ResourceFormat::RGBA8Int,        "RGBA8Int",         4,  4, FormatType::Sint,      false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::RGBA8Uint,       "RGBA8Uint",        4,  4, FormatType::Uint,      false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::RGBA16Int,       "RGBA16Int",        8,  4, FormatType::Sint,      false, false, false, {1, 1}, {16, 16, 16, 16}},
    {ResourceFormat::RGBA16Uint,      "RGBA16Uint",       8,  4, FormatType::Uint,      false, false, false, {1, 1}, {16, 16, 16, 16}},
    {ResourceFormat::RGBA32Int,       "RGBA32Int",        16, 4, FormatType::Sint,      false, false, false, {1, 1}, {32, 32, 32, 32}},
    {ResourceFormat::RGBA32Uint,      "RGBA32Uint",       16, 4, FormatType::Uint,      false, false, false, {1, 1}, {32, 32, 32, 32}},
    {ResourceFormat::BGRA4Unorm,      "BGRA4Unorm",       2,  4, FormatType::Unorm,     false, false, false, {1, 1}, {4, 4, 4, 4}},
    {ResourceFormat::BGRA8Unorm,      "BGRA8Unorm",       4,  4, FormatType::Unorm,     false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::BGRA8UnormSrgb,  "BGRA8UnormSrgb",   4,  4, FormatType::UnormSrgb, false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::BGRX8Unorm,      "BGRX8Unorm",       4,  4, FormatType::Unorm,     false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::BGRX8UnormSrgb,  "BGRX8UnormSrgb",   4,  4, FormatType::UnormSrgb, false, false, false, {1, 1}, {8, 8, 8, 8}},
    {ResourceFormat::R5G6B5Unorm,     "R5G6B5Unorm",      2,  3, FormatType::Unorm,     false, false, false, {1, 1}, {5, 6, 5, 0}},
    {ResourceFormat::D32Float,        "D32Float",         4,  1, FormatType::Float,     true,  false, false, {1, 1}, {32, 0, 0, 0}},
    {ResourceFormat::D32FloatS8Uint,  "D32FloatS8Uint",   4,  1, FormatType::Float,     true,  true,  false, {1, 1}, {32, 0, 0, 0}},
    {ResourceFormat::D16Unorm,        "D16Unorm",         2,  1, FormatType::Unorm,     true,  false, false, {1, 1}, {16, 0, 0, 0}},
    {ResourceFormat::BC1Unorm,        "BC1Unorm",         8,  3, FormatType::Unorm,     false, false, true,  {4, 4}, {64, 0, 0, 0}},
    {ResourceFormat::BC1UnormSrgb,    "BC1UnormSrgb",     8,  3, FormatType::UnormSrgb, false, false, true,  {4, 4}, {64, 0, 0, 0}},
    {ResourceFormat::BC2Unorm,        "BC2Unorm",         16, 4, FormatType::Unorm,     false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC2UnormSrgb,    "BC2UnormSrgb",     16, 4, FormatType::UnormSrgb, false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC3Unorm,        "BC3Unorm",         16, 4, FormatType::Unorm,     false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC3UnormSrgb,    "BC3UnormSrgb",     16, 4, FormatType::UnormSrgb, false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC4Unorm,        "BC4Unorm",         8,  1, FormatType::Unorm,     false, false, true,  {4, 4}, {64, 0, 0, 0}},
    {ResourceFormat::BC4Snorm,        "BC4Snorm",         8,  1, FormatType::Snorm,     false, false, true,  {4, 4}, {64, 0, 0, 0}},
    {ResourceFormat::BC5Unorm,        "BC5Unorm",         16, 2, FormatType::Unorm,     false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC5Snorm,        "BC5Snorm",         16, 2, FormatType::Snorm,     false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC6HS16,         "BC6HS16",          16, 3, FormatType::Float,     false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC6HU16,         "BC6HU16",          16, 3, FormatType::Float,     false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC7Unorm,        "BC7Unorm",         16, 4, FormatType::Unorm,     false, false, true,  {4, 4}, {128, 0, 0, 0}},
    {ResourceFormat::BC7UnormSrgb,    "BC7UnormSrgb",     16, 4, FormatType::UnormSrgb, false, false, true,  {4, 4}, {128, 0, 0, 0}},
    // clang-format on
};
static_assert(std::size(kFormatDesc) == std::to_underlying(ResourceFormat::Count));

namespace
{
constexpr std::array kNativeFormatDescs{
    vk::Format::eUndefined,
    vk::Format::eR8Unorm,
    vk::Format::eR8Snorm,
    vk::Format::eR16Unorm,
    vk::Format::eR16Snorm,
    vk::Format::eR8G8Unorm,
    vk::Format::eR8G8Snorm,
    vk::Format::eR16G16Unorm,
    vk::Format::eR16G16Snorm,
    vk::Format::eB5G5R5A1UnormPack16,
    vk::Format::eR8G8B8A8Unorm,
    vk::Format::eR8G8B8A8Snorm,
    vk::Format::eA2B10G10R10UnormPack32,
    vk::Format::eA2B10G10R10UintPack32,
    vk::Format::eR16G16B16A16Unorm,
    vk::Format::eR16G16B16A16Snorm,
    vk::Format::eR8G8B8A8Srgb,
    vk::Format::eR16Sfloat,
    vk::Format::eR16G16Sfloat,
    vk::Format::eR16G16B16A16Sfloat,
    vk::Format::eR32Sfloat,
    vk::Format::eR32G32Sfloat,
    vk::Format::eR32G32B32Sfloat,
    vk::Format::eR32G32B32A32Sfloat,
    vk::Format::eB10G11R11UfloatPack32,
    vk::Format::eE5B9G9R9UfloatPack32,
    vk::Format::eR8Sint,
    vk::Format::eR8Uint,
    vk::Format::eR16Sint,
    vk::Format::eR16Uint,
    vk::Format::eR32Sint,
    vk::Format::eR32Uint,
    vk::Format::eR8G8Sint,
    vk::Format::eR8G8Uint,
    vk::Format::eR16G16Sint,
    vk::Format::eR16G16Uint,
    vk::Format::eR32G32Sint,
    vk::Format::eR32G32Uint,
    vk::Format::eR32G32B32Sint,
    vk::Format::eR32G32B32Uint,
    vk::Format::eR8G8B8A8Sint,
    vk::Format::eR8G8B8A8Uint,
    vk::Format::eR16G16B16A16Sint,
    vk::Format::eR16G16B16A16Uint,
    vk::Format::eR32G32B32A32Sint,
    vk::Format::eR32G32B32A32Uint,
    vk::Format::eB4G4R4A4UnormPack16,
    vk::Format::eB8G8R8A8Unorm,
    vk::Format::eB8G8R8A8Srgb,
    vk::Format::eUndefined,
    vk::Format::eUndefined,
    vk::Format::eR5G6B5UnormPack16,
    vk::Format::eD32Sfloat,
    vk::Format::eD32SfloatS8Uint,
    vk::Format::eD16Unorm,
    vk::Format::eBc1RgbaUnormBlock,
    vk::Format::eBc1RgbaSrgbBlock,
    vk::Format::eBc2UnormBlock,
    vk::Format::eBc2SrgbBlock,
    vk::Format::eBc3UnormBlock,
    vk::Format::eBc3SrgbBlock,
    vk::Format::eBc4UnormBlock,
    vk::Format::eBc4SnormBlock,
    vk::Format::eBc5UnormBlock,
    vk::Format::eBc5SnormBlock,
    vk::Format::eBc6HSfloatBlock,
    vk::Format::eBc6HUfloatBlock,
    vk::Format::eBc7UnormBlock,
    vk::Format::eBc7SrgbBlock,
};
static_assert(kNativeFormatDescs.size() == std::to_underlying(ResourceFormat::Count));

constexpr std::array kSrgbFormats{
    std::pair{ResourceFormat::BC1Unorm, ResourceFormat::BC1UnormSrgb},
    std::pair{ResourceFormat::BC2Unorm, ResourceFormat::BC2UnormSrgb},
    std::pair{ResourceFormat::BC3Unorm, ResourceFormat::BC3UnormSrgb},
    std::pair{ResourceFormat::BGRA8Unorm, ResourceFormat::BGRA8UnormSrgb},
    std::pair{ResourceFormat::BGRX8Unorm, ResourceFormat::BGRX8UnormSrgb},
    std::pair{ResourceFormat::RGBA8Unorm, ResourceFormat::RGBA8UnormSrgb},
    std::pair{ResourceFormat::BC7Unorm, ResourceFormat::BC7UnormSrgb},
};

constexpr std::array kResourceBindFlags{
    std::pair{ResourceBindFlags::None, "None"},
    std::pair{ResourceBindFlags::Vertex, "Vertex"},
    std::pair{ResourceBindFlags::Index, "Index"},
    std::pair{ResourceBindFlags::Constant, "Constant"},
    std::pair{ResourceBindFlags::StreamOutput, "StreamOutput"},
    std::pair{ResourceBindFlags::ShaderResource, "ShaderResource"},
    std::pair{ResourceBindFlags::UnorderedAccess, "UnorderedAccess"},
    std::pair{ResourceBindFlags::RenderTarget, "RenderTarget"},
    std::pair{ResourceBindFlags::DepthStencil, "DepthStencil"},
    std::pair{ResourceBindFlags::IndirectArg, "IndirectArg"},
    std::pair{ResourceBindFlags::Shared, "Shared"},
    std::pair{ResourceBindFlags::AccelerationStructure, "AccelerationStructure"},
};

constexpr std::array kTextureChannelFlags{
    std::pair{TextureChannelFlags::None, "None"},
    std::pair{TextureChannelFlags::Red, "Red"},
    std::pair{TextureChannelFlags::Green, "Green"},
    std::pair{TextureChannelFlags::Blue, "Blue"},
    std::pair{TextureChannelFlags::Alpha, "Alpha"},
    std::pair{TextureChannelFlags::RGB, "RGB"},
    std::pair{TextureChannelFlags::RGBA, "RGBA"},
};

constexpr std::array kMemoryTypes{
    std::pair{MemoryType::DeviceLocal, "DeviceLocal"},
    std::pair{MemoryType::Upload, "Upload"},
    std::pair{MemoryType::ReadBack, "ReadBack"},
};
}  // namespace

vk::Format getVulkanFormat(ResourceFormat format)
{
    const auto index = std::to_underlying(format);
    DCHECK(index < kNativeFormatDescs.size());
    const auto native = kNativeFormatDescs[index];
    CHECK(
        format == ResourceFormat::Unknown || native != vk::Format::eUndefined,
        "resource format {} is unavailable in Vulkan",
        index
    );
    return native;
}

ResourceFormat getResourceFormat(vk::Format format)
{
    const auto found = std::ranges::find(kNativeFormatDescs, format);
    CHECK(found != kNativeFormatDescs.end(), "unsupported render resource format {}", vk::to_string(format));
    return static_cast<ResourceFormat>(found - kNativeFormatDescs.begin());
}

uint32_t getNumChannelBits(ResourceFormat format, TextureChannelFlags mask)
{
    const auto& bits  = kFormatDesc[std::to_underlying(format)].numChannelBits;
    uint32_t    count = 0;
    for (size_t channel = 0; channel < std::size(bits); ++channel)
        if (is_set(mask, static_cast<TextureChannelFlags>(1u << channel)))
            count += bits[channel];
    return count;
}

TextureChannelFlags getChannelMask(ResourceFormat format)
{
    const auto& bits = kFormatDesc[std::to_underlying(format)].numChannelBits;
    auto        mask = TextureChannelFlags::None;
    for (size_t channel = 0; channel < std::size(bits); ++channel)
        if (bits[channel] > 0)
            mask |= static_cast<TextureChannelFlags>(1u << channel);
    return mask;
}

uint32_t getFormatRowPitch(ResourceFormat format, uint32_t width)
{
    const auto blockWidth = getFormatWidthCompressionRatio(format);
    DCHECK(width % blockWidth == 0);
    return width / blockWidth * getFormatBytesPerBlock(format);
}

ResourceFormat srgbToLinearFormat(ResourceFormat format)
{
    for (const auto& [linear, srgb] : kSrgbFormats)
        if (format == srgb)
            return linear;
    DCHECK(!isSrgbFormat(format));
    return format;
}

ResourceFormat linearToSrgbFormat(ResourceFormat format)
{
    for (const auto& [linear, srgb] : kSrgbFormats)
        if (format == linear)
            return srgb;
    return format;
}

ResourceFormat depthToColorFormat(ResourceFormat format)
{
    switch (format)
    {
        case ResourceFormat::D16Unorm:
            return ResourceFormat::R16Unorm;
        case ResourceFormat::D32Float:
            return ResourceFormat::R32Float;
        default:
            DCHECK(!isDepthFormat(format));
            return format;
    }
}

bool doesFormatHaveAlpha(ResourceFormat format)
{
    return getFormatChannelCount(format) == 4 && format != ResourceFormat::BGRX8Unorm &&
        format != ResourceFormat::BGRX8UnormSrgb;
}

std::string to_string(FormatType type)
{
    static constexpr const char* kNames[]{"Unknown", "Float", "Unorm", "UnormSrgb", "Snorm", "Uint", "Sint"};
    const auto                   index = std::to_underlying(type);
    DCHECK(index >= 0 && static_cast<size_t>(index) < std::size(kNames));
    return kNames[index];
}

std::string to_string(ResourceBindFlags flags)
{
    if (flags == ResourceBindFlags::None)
        return kResourceBindFlags.front().second;

    std::string result;
    for (const auto& [flag, name] : kResourceBindFlags)
    {
        // Match Falcor's resource flag names in diagnostic strings.
        if (flag == ResourceBindFlags::Shared || !is_set(flags, flag))
            continue;
        if (!result.empty())
            result += " | ";
        result += name;
    }
    return result;
}

FALCOR_SCRIPT_BINDING(Formats)
{
    const PythonObject enums{PyImport_ImportModule("enum")};
    if (!enums)
        return -1;

    const auto bind = [m, &enums](const char* enumClass, const char* name, const auto& entries)
    {
        using Enum = std::ranges::range_value_t<decltype(entries)>::first_type;
        const PythonObject values{PyDict_New()};
        if (!values)
            return -1;

        for (const auto& [value, label] : entries)
        {
            const auto number = ScriptBindings::toPython(std::to_underlying(value));
            const auto key    = std::string_view(label) == "None" ? "None_" : label;
            if (!number || PyDict_SetItemString(values.get(), key, number.get()) != 0)
                return -1;
        }

        const PythonObject type{PyObject_CallMethod(enums.get(), enumClass, "sO", name, values.get())};
        if (!type || ScriptBindings::registerType(type.get(), typeid(Enum)) != 0)
            return -1;
        return PyModule_AddObjectRef(m, name, type.get());
    };

    const auto formats = std::views::transform(
        kFormatDesc,
        [](const FormatDesc& desc) { return std::pair{desc.format, desc.name.c_str()}; }
    );
    if (bind("IntFlag", "ResourceBindFlags", kResourceBindFlags) != 0 ||
        bind("IntEnum", "ResourceFormat", formats) != 0 ||
        bind("IntEnum", "MemoryType", kMemoryTypes) != 0)
        return -1;
    return bind("IntFlag", "TextureChannelFlags", kTextureChannelFlags);
}
}  // namespace Falcor
