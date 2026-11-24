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
#include "render/graph/RenderGraphIR.h"

#include "core/ScriptWriter.h"

#include <algorithm>

namespace Falcor
{
namespace
{
constexpr char kGraphPrefix[] = "    g.";
}

RenderGraphIR::RenderGraphIR(const std::string& name) :
    mIR("from falcor import *\n\ndef " + getFuncName(name) + "():\n")
{
    mIR += "    g = " + ScriptWriter::makeFunc("RenderGraph", name);
}

void RenderGraphIR::createPass(const std::string& passClass, const std::string& passName, const Properties& props)
{
    const PythonObject dict{props.toPython()};
    mIR += kGraphPrefix + ScriptWriter::makeFunc("create_pass", passName, passClass, dict.get());
}

void RenderGraphIR::addEdge(const std::string& src, const std::string& dst)
{
    mIR += kGraphPrefix + ScriptWriter::makeFunc("add_edge", src, dst);
}

void RenderGraphIR::markOutput(const std::string& name, TextureChannelFlags mask)
{
    if (mask == TextureChannelFlags::RGB)
    {
        mIR += kGraphPrefix + ScriptWriter::makeFunc("mark_output", name);
        return;
    }

    mIR += kGraphPrefix + ScriptWriter::makeFunc("mark_output", name, mask);
}

std::string RenderGraphIR::getFuncName(const std::string& graphName)
{
    auto name = "render_graph_" + (graphName.empty() ? "renderGraph" : graphName);
    std::ranges::replace_if(name, [](char c) { return c == ' ' || c == '/' || c == '\\'; }, '_');
    return name;
}
}  // namespace Falcor
