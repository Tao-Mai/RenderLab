#pragma once

#include "base/gfx/mesh.h"
#include "base/gfx/mesh_gpu.h"

// 可绘制网格组件：CPU 网格 + GPU 缓冲 + 材质。着色由 demo 自己完成。
struct MeshRenderer
{
    Mesh     mesh;
    MeshGPU  gpu;
    Material material{};

    void upload(bool dynamic = false) { gpu.upload(mesh, dynamic); }
    void update_gpu() { gpu.update_vertices(mesh); }
    void draw() const { gpu.draw_triangles(); }
};
