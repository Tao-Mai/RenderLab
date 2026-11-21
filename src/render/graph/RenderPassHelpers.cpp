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
#include "render/graph/RenderPassHelpers.h"

#include "core/Logger.h"
#include "render/graph/RenderPass.h"
#include "render/resources/ResourceViews.h"
#include "render/resources/Texture.h"

namespace Falcor
{
uint2 RenderPassHelpers::calculateIOSize(IOSize selection, uint2 fixedSize, uint2 windowSize)
{
    switch (selection)
    {
        case IOSize::Default: return {};
        case IOSize::Fixed: return fixedSize;
        case IOSize::Full: return windowSize;
        case IOSize::Half: return windowSize / uint2{2};
        case IOSize::Quarter: return windowSize / uint2{4};
        case IOSize::Double: return windowSize * uint2{2};
    }
    DCHECK(false);
    return {};
}

DefineList getValidResourceDefines(const ChannelList& channels, const RenderData& data, const std::string& prefix)
{
    DefineList defines;
    for (const auto& channel : channels)
        if (channel.optional && !channel.texname.empty())
            defines.add(prefix + channel.texname, data[channel.name] ? "1" : "0");
    return defines;
}

void addRenderPassInputs(
    RenderPassReflection& reflector, const ChannelList& channels, ResourceBindFlags bindFlags, uint2 dim
)
{
    for (const auto& channel : channels)
    {
        auto& field = reflector.addInput(channel.name, channel.desc)
            .texture2D(dim.x, dim.y)
            .bindFlags(bindFlags)
            .format(channel.format);
        if (channel.optional)
            field.flags(RenderPassReflection::Field::Flags::Optional);
    }
}

void addRenderPassOutputs(
    RenderPassReflection& reflector, const ChannelList& channels, ResourceBindFlags bindFlags, uint2 dim
)
{
    for (const auto& channel : channels)
    {
        auto& field = reflector.addOutput(channel.name, channel.desc)
            .texture2D(dim.x, dim.y)
            .bindFlags(bindFlags)
            .format(channel.format);
        if (channel.optional)
            field.flags(RenderPassReflection::Field::Flags::Optional);
    }
}

void clearRenderPassChannels(RenderContext* context, const ChannelList& channels, const RenderData& data)
{
    for (const auto& channel : channels)
    {
        const auto texture = data.getTexture(channel.name);
        if (!texture)
            continue;

        const auto view = texture->getUAV();
        if (isIntegerFormat(texture->getFormat()))
            context->clearUAV(view.get(), uint4(0));
        else
            context->clearUAV(view.get(), float4(0.f));
    }
}
}  // namespace Falcor
