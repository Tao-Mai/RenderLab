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
#include "render/graph/RenderGraphImportExport.h"

#include "core/Logger.h"
#include "core/ScriptWriter.h"
#include "core/Scripting.h"
#include "render/graph/RenderGraph.h"
#include "render/graph/RenderGraphIR.h"

#include <algorithm>
#include <fstream>
#include <utility>

namespace
{
    void updateGraphStrings(std::string& graphName, std::filesystem::path& path, std::string& funcName)
    {
        if (graphName.empty())
            graphName = "renderGraph";
        if (path.empty())
            path = graphName + '.' + RenderGraph::kFileExtensionFilters.front().ext;
        if (funcName.empty())
            funcName = RenderGraphIR::getFuncName(graphName);
    }

}  // namespace

ref<RenderGraph> RenderGraphImporter::import(std::string graphName, std::filesystem::path path, std::string funcName)
{
    updateGraphStrings(graphName, path, funcName);
    Scripting::Context context;
    Scripting::runScriptFromFile(path, context);
    const auto variable = "globals()[" + ScriptWriter::getArgString(graphName) + ']';
    Scripting::runScript(variable + " = " + ScriptWriter::makeFunc(funcName), context);

    auto graph = context.getObject<ref<RenderGraph>>(graphName);
    graph->setName(graphName);
    return graph;
}

std::vector<ref<RenderGraph>> RenderGraphImporter::importAllGraphs(const std::filesystem::path& path)
{
    Scripting::Context context;
    Scripting::runScriptFromFile(path, context);

    auto                          objects = context.getObjects<ref<RenderGraph>>();
    std::vector<ref<RenderGraph>> graphs;
    graphs.reserve(objects.size());
    for (auto& [name, graph] : objects)
    {
        graph->setName(name);
        graphs.push_back(std::move(graph));
    }
    return graphs;
}

std::string RenderGraphExporter::getIR(const ref<RenderGraph>& graph)
{

    RenderGraphIR ir(graph->getName());
    for (uint32_t id = 0; id < graph->mpGraph->getCurrentNodeId(); ++id)
    {
        if (!graph->mpGraph->doesNodeExist(id))
            continue;
        const auto& node = graph->mNodeData.at(id);
        ir.createPass(node.pPass->getType(), node.name, node.pPass->getProperties());
    }

    for (uint32_t id = 0; id < graph->mpGraph->getCurrentEdgeId(); ++id)
    {
        if (!graph->mpGraph->doesEdgeExist(id))
            continue;
        const auto& data   = graph->mEdgeData.at(id);
        const auto& edge   = *graph->mpGraph->getEdge(id);
        const auto& source = graph->mNodeData.at(edge.getSourceNode()).name;
        const auto& target = graph->mNodeData.at(edge.getDestNode()).name;
        ir.addEdge(
            data.srcField.empty() ? source : source + '.' + data.srcField,
            data.dstField.empty() ? target : target + '.' + data.dstField
        );
    }

    for (size_t index = 0; index < graph->getOutputCount(); ++index)
    {
        const auto                       name  = graph->getOutputName(index);
        const auto                       masks = graph->getOutputMasks(index);
        std::vector<TextureChannelFlags> sorted(masks.begin(), masks.end());
        std::ranges::sort(sorted);
        for (const auto mask : sorted)
            ir.markOutput(name, mask);
    }
    return ir.getIR();
}

std::string RenderGraphExporter::getFuncName(const std::string& graphName)
{
    return RenderGraphIR::getFuncName(graphName);
}

bool RenderGraphExporter::save(const ref<RenderGraph>& graph, std::filesystem::path path)
{

    auto        graphName = graph->getName();
    std::string funcName;
    updateGraphStrings(graphName, path, funcName);

    std::ofstream file(path, std::ios::binary);
    CHECK(file, "cannot write render graph '{}'", path.string());
    const ScriptWriter::VariableName variable("globals()[" + ScriptWriter::getArgString(graphName) + ']');
    file << getIR(graph) << '\n';
    file << variable.name << " = " << ScriptWriter::makeFunc(funcName);
    file << "try: " << ScriptWriter::makeMemberFunc("m", "addGraph", variable);
    file << "except NameError: pass\n";
    file.close();
    CHECK(file, "failed to write render graph '{}'", path.string());
    return true;
}
