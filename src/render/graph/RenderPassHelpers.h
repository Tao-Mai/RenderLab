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
#include "core/MetadataAnnotations.h"
#include "core/math/Vector.h"
#include "core/program/DefineList.h"
#include "render/resources/Formats.h"

#include <string>
#include <vector>

namespace Falcor
{
class RenderContext;

class RenderData;
class RenderPassReflection;

struct FALCOR_API RenderPassHelpers
{
    enum class RL_ENUM IOSize : uint32_t
    {
        Default RL_ENUM_VALUE("Default"),
        Fixed RL_ENUM_VALUE("Fixed"),
        Full RL_ENUM_VALUE("Full"),
        Half RL_ENUM_VALUE("Half"),
        Quarter RL_ENUM_VALUE("Quarter"),
        Double RL_ENUM_VALUE("Double"),
    };

    static uint2 calculateIOSize(IOSize selection, uint2 fixedSize, uint2 windowSize);
};

struct ChannelDesc
{
    std::string name;
    std::string texname;
    std::string desc;
    bool optional = false;
    ResourceFormat format = ResourceFormat::Unknown;
};

using ChannelList = std::vector<ChannelDesc>;

DefineList getValidResourceDefines(
    const ChannelList& channels, const RenderData& renderData, const std::string& prefix = "is_valid_"
);

void addRenderPassInputs(
    RenderPassReflection& reflector,
    const ChannelList& channels,
    ResourceBindFlags bindFlags = ResourceBindFlags::ShaderResource,
    uint2 dim = {}
);
void addRenderPassOutputs(
    RenderPassReflection& reflector,
    const ChannelList& channels,
    ResourceBindFlags bindFlags = ResourceBindFlags::UnorderedAccess,
    uint2 dim = {}
);
void clearRenderPassChannels(RenderContext* context, const ChannelList& channels, const RenderData& renderData);
}  // namespace Falcor

#include RL_METADATA_HEADER("metadata/src/render/graph/RenderPassHelpers.generated.h")
