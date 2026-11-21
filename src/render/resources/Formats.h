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
#pragma once

#include "core/Macros.h"

#include <cstdint>
#include <string>

namespace Falcor
{
enum class TextureChannelFlags : uint32_t
{
    None  = 0x0,
    Red   = 0x1,
    Green = 0x2,
    Blue  = 0x4,
    Alpha = 0x8,
    RGB   = 0x7,
    RGBA  = 0xf,
};

FALCOR_ENUM_CLASS_OPERATORS(TextureChannelFlags);

enum class ResourceBindFlags : uint32_t
{
    None            = 0x0,    ///< The resource will not be bound the pipeline. Use this to create a staging resource
    Vertex          = 0x1,    ///< The resource will be bound as a vertex-buffer
    Index           = 0x2,    ///< The resource will be bound as a index-buffer
    Constant        = 0x4,    ///< The resource will be bound as a constant-buffer
    StreamOutput    = 0x8,    ///< The resource will be bound to the stream-output stage as an output buffer
    ShaderResource  = 0x10,   ///< The resource will be bound as a shader-resource
    UnorderedAccess = 0x20,   ///< The resource will be bound as an UAV
    RenderTarget    = 0x40,   ///< The resource will be bound as a render-target
    DepthStencil    = 0x80,   ///< The resource will be bound as a depth-stencil buffer
    IndirectArg     = 0x100,  ///< The resource will be bound as an indirect argument buffer
    Shared =
        0x200,  ///< The resource will be shared with a different adapter. Mostly useful for sharing resoures with CUDA
    AccelerationStructure = 0x80000000,  ///< The resource will be bound as an acceleration structure

    AllColorViews = ShaderResource | UnorderedAccess | RenderTarget,
    AllDepthViews = ShaderResource | DepthStencil
};

FALCOR_ENUM_CLASS_OPERATORS(ResourceBindFlags);

/**
 * Resource formats
 */
enum class ResourceFormat : uint32_t
{
    Unknown,
    R8Unorm,
    R8Snorm,
    R16Unorm,
    R16Snorm,
    RG8Unorm,
    RG8Snorm,
    RG16Unorm,
    RG16Snorm,
    RGB5A1Unorm,
    RGBA8Unorm,
    RGBA8Snorm,
    RGB10A2Unorm,
    RGB10A2Uint,
    RGBA16Unorm,
    RGBA16Snorm,
    RGBA8UnormSrgb,
    R16Float,
    RG16Float,
    RGBA16Float,
    R32Float,
    RG32Float,
    RGB32Float,
    RGBA32Float,
    R11G11B10Float,
    RGB9E5Float,
    R8Int,
    R8Uint,
    R16Int,
    R16Uint,
    R32Int,
    R32Uint,
    RG8Int,
    RG8Uint,
    RG16Int,
    RG16Uint,
    RG32Int,
    RG32Uint,
    RGB32Int,
    RGB32Uint,
    RGBA8Int,
    RGBA8Uint,
    RGBA16Int,
    RGBA16Uint,
    RGBA32Int,
    RGBA32Uint,

    BGRA4Unorm,
    BGRA8Unorm,
    BGRA8UnormSrgb,

    BGRX8Unorm,
    BGRX8UnormSrgb,
    R5G6B5Unorm,

    // Depth-stencil
    D32Float,
    D32FloatS8Uint,
    D16Unorm,

    // Compressed formats
    BC1Unorm,  // DXT1
    BC1UnormSrgb,
    BC2Unorm,  // DXT3
    BC2UnormSrgb,
    BC3Unorm,  // DXT5
    BC3UnormSrgb,
    BC4Unorm,  // RGTC Unsigned Red
    BC4Snorm,  // RGTC Signed Red
    BC5Unorm,  // RGTC Unsigned RG
    BC5Snorm,  // RGTC Signed RG
    BC6HS16,
    BC6HU16,
    BC7Unorm,
    BC7UnormSrgb,

    Count
};

enum class FormatType
{
    Unknown,
    Float,
    Unorm,
    UnormSrgb,
    Snorm,
    Uint,
    Sint,
};

struct FormatDesc
{
    ResourceFormat    format;
    const std::string name;
    uint32_t          bytesPerBlock;
    uint32_t          channelCount;
    FormatType        type;
    bool              isDepth;
    bool              isStencil;
    bool              isCompressed;
    struct
    {
        uint32_t width;
        uint32_t height;
    } compressionRatio;
    int numChannelBits[4];
};

extern const FALCOR_API FormatDesc kFormatDesc[];

[[nodiscard]] inline uint32_t getFormatBytesPerBlock(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].bytesPerBlock;
}

[[nodiscard]] inline uint32_t getFormatWidthCompressionRatio(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].compressionRatio.width;
}

[[nodiscard]] inline uint32_t getFormatHeightCompressionRatio(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].compressionRatio.height;
}

[[nodiscard]] inline uint32_t getFormatPixelsPerBlock(ResourceFormat format)
{
    return getFormatWidthCompressionRatio(format) * getFormatHeightCompressionRatio(format);
}

[[nodiscard]] inline bool isDepthFormat(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].isDepth;
}
[[nodiscard]] inline bool isStencilFormat(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].isStencil;
}
[[nodiscard]] inline bool isDepthStencilFormat(ResourceFormat format)
{
    return isDepthFormat(format) || isStencilFormat(format);
}
[[nodiscard]] inline bool isCompressedFormat(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].isCompressed;
}

[[nodiscard]] inline uint32_t getFormatChannelCount(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].channelCount;
}

[[nodiscard]] inline FormatType getFormatType(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].type;
}

[[nodiscard]] inline bool isIntegerFormat(ResourceFormat format)
{
    const auto type = getFormatType(format);
    return type == FormatType::Uint || type == FormatType::Sint;
}

[[nodiscard]] inline uint32_t getNumChannelBits(ResourceFormat format, int channel)
{
    return kFormatDesc[std::to_underlying(format)].numChannelBits[channel];
}

[[nodiscard]] uint32_t            getNumChannelBits(ResourceFormat format, TextureChannelFlags mask);
[[nodiscard]] TextureChannelFlags getChannelMask(ResourceFormat format);
[[nodiscard]] uint32_t            getFormatRowPitch(ResourceFormat format, uint32_t width);

[[nodiscard]] inline bool isSrgbFormat(ResourceFormat format) { return getFormatType(format) == FormatType::UnormSrgb; }

[[nodiscard]] ResourceFormat srgbToLinearFormat(ResourceFormat format);
[[nodiscard]] ResourceFormat linearToSrgbFormat(ResourceFormat format);
[[nodiscard]] ResourceFormat depthToColorFormat(ResourceFormat format);
[[nodiscard]] bool           doesFormatHaveAlpha(ResourceFormat format);

[[nodiscard]] inline const std::string& to_string(ResourceFormat format)
{
    return kFormatDesc[std::to_underlying(format)].name;
}

[[nodiscard]] std::string to_string(FormatType type);
[[nodiscard]] std::string to_string(ResourceBindFlags flags);
}  // namespace Falcor
