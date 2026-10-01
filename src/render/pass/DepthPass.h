//
// Created by 24500 on 2026/10/1.
//

#pragma once
#include "RenderGraph.h"

class DepthPass
{
public:
    static ResourceHandle src;
    static ResourceHandle dst;

    static void registerPass(RenderGraph& graph)
    {
        graph.registerPass("depth_pass", 1, 1);
    }

    static void setupPass(RenderGraph& graph)
    {
        // graph.alloc ..
        // graph.bind ..
    }

    static void executePass(RenderGraph& graph)
    {
        // graph.get ..
    }
};
