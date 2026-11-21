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

#include "core/Logger.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Falcor
{
class DirectedGraph
{
public:
    static constexpr uint32_t kInvalidID = ~0u;

    class Node
    {
    public:
        Node() = default;

        [[nodiscard]] uint32_t getOutgoingEdgeCount() const { return static_cast<uint32_t>(mOutgoingEdges.size()); }
        [[nodiscard]] uint32_t getIncomingEdgeCount() const { return static_cast<uint32_t>(mIncomingEdges.size()); }
        [[nodiscard]] uint32_t getOutgoingEdge(uint32_t index) const { return mOutgoingEdges[index]; }
        [[nodiscard]] uint32_t getIncomingEdge(uint32_t index) const { return mIncomingEdges[index]; }

    private:
        friend class DirectedGraph;
        std::vector<uint32_t> mIncomingEdges;
        std::vector<uint32_t> mOutgoingEdges;
    };

    class Edge
    {
    public:
        Edge() = default;

        [[nodiscard]] uint32_t getSourceNode() const { return mSrc; }
        [[nodiscard]] uint32_t getDestNode() const { return mDst; }

    private:
        friend class DirectedGraph;
        Edge(uint32_t source, uint32_t target) : mSrc(source), mDst(target) {}

        uint32_t mSrc = kInvalidID;
        uint32_t mDst = kInvalidID;
    };

    uint32_t addNode()
    {
        const auto id = mCurrentNodeId++;
        mNodes.try_emplace(id);
        return id;
    }

    uint32_t addEdge(uint32_t source, uint32_t target)
    {
        DCHECK(doesNodeExist(source) && doesNodeExist(target));
        const auto id = mCurrentEdgeId++;
        mEdges.emplace(id, Edge{source, target});
        mNodes.at(source).mOutgoingEdges.push_back(id);
        mNodes.at(target).mIncomingEdges.push_back(id);
        return id;
    }

    std::unordered_set<uint32_t> removeNode(uint32_t id)
    {
        const auto&                  node = *getNode(id);
        std::unordered_set<uint32_t> removedEdges(node.mIncomingEdges.begin(), node.mIncomingEdges.end());
        removedEdges.insert(node.mOutgoingEdges.begin(), node.mOutgoingEdges.end());
        for (const auto edge : removedEdges)
            removeEdge(edge);
        mNodes.erase(id);
        return removedEdges;
    }

    void removeEdge(uint32_t id)
    {
        const auto& edge = *getEdge(id);
        std::erase(mNodes.at(edge.getSourceNode()).mOutgoingEdges, id);
        std::erase(mNodes.at(edge.getDestNode()).mIncomingEdges, id);
        mEdges.erase(id);
    }

    [[nodiscard]] bool     doesNodeExist(uint32_t id) const { return mNodes.contains(id); }
    [[nodiscard]] bool     doesEdgeExist(uint32_t id) const { return mEdges.contains(id); }
    [[nodiscard]] uint32_t getCurrentNodeId() const { return mCurrentNodeId; }
    [[nodiscard]] uint32_t getCurrentEdgeId() const { return mCurrentEdgeId; }

    [[nodiscard]] const Node* getNode(uint32_t id) const
    {
        const auto found = mNodes.find(id);
        DCHECK(found != mNodes.end());
        return &found->second;
    }

    [[nodiscard]] const Edge* getEdge(uint32_t id) const
    {
        const auto found = mEdges.find(id);
        DCHECK(found != mEdges.end());
        return &found->second;
    }

private:
    std::unordered_map<uint32_t, Node> mNodes;
    std::unordered_map<uint32_t, Edge> mEdges;
    uint32_t                           mCurrentNodeId = 0;
    uint32_t                           mCurrentEdgeId = 0;
};
}  // namespace Falcor
