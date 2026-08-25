#pragma once

#include "base/gfx/mesh.h"

#include <glad/glad.h>

#include <utility>

class MeshGPU
{
public:
    GLuint  vao          = 0;
    GLuint  vbo          = 0;
    GLuint  ebo          = 0;
    GLsizei vertex_count = 0;
    GLsizei index_count  = 0;

    void upload(const Mesh& mesh, bool dynamic_vertices = false)
    {
        const auto& vertices = mesh.vertices;
        const auto& indices  = mesh.indices;
        vertex_count         = static_cast<GLsizei>(vertices.size());
        index_count          = static_cast<GLsizei>(indices.size());

        glCreateVertexArrays(1, &vao);
        glCreateBuffers(1, &vbo);
        glCreateBuffers(1, &ebo);

        const GLenum vertex_usage = dynamic_vertices ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
        glNamedBufferData(vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                          vertex_usage);
        glNamedBufferData(ebo, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(),
                          GL_STATIC_DRAW);

        glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
        glEnableVertexArrayAttrib(vao, 0);
        glEnableVertexArrayAttrib(vao, 1);
        glEnableVertexArrayAttrib(vao, 2);
        glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, pos));
        glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
        glVertexArrayAttribFormat(vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, uv));
        glVertexArrayAttribBinding(vao, 0, 0);
        glVertexArrayAttribBinding(vao, 1, 0);
        glVertexArrayAttribBinding(vao, 2, 0);
        glVertexArrayElementBuffer(vao, ebo);
    }

    void update_vertices(const Mesh& mesh)
    {
        if (vbo == 0)
            return;
        const auto& vertices = mesh.vertices;
        vertex_count         = static_cast<GLsizei>(vertices.size());
        glNamedBufferSubData(vbo, 0, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                             vertices.data());
    }

    void draw_triangles() const
    {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void destroy()
    {
        if (ebo != 0)
            glDeleteBuffers(1, &ebo);
        if (vbo != 0)
            glDeleteBuffers(1, &vbo);
        if (vao != 0)
            glDeleteVertexArrays(1, &vao);
        vao = vbo = ebo = 0;
        vertex_count = index_count = 0;
    }

    ~MeshGPU() { destroy(); }

    MeshGPU()                         = default;
    MeshGPU(const MeshGPU&)            = delete;
    MeshGPU& operator=(const MeshGPU&) = delete;

    MeshGPU(MeshGPU&& other) noexcept
    {
        *this = std::move(other);
    }

    MeshGPU& operator=(MeshGPU&& other) noexcept
    {
        if (this != &other)
        {
            destroy();
            vao          = other.vao;
            vbo          = other.vbo;
            ebo          = other.ebo;
            vertex_count = other.vertex_count;
            index_count  = other.index_count;
            other.vao = other.vbo = other.ebo = 0;
            other.vertex_count = other.index_count = 0;
        }
        return *this;
    }
};
